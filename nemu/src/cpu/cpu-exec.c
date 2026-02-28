/***************************************************************************************
* Copyright (c) 2014-2022 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
# cpu仿真一cycle.
***************************************************************************************/

#include <cpu/cpu.h>
#include <cpu/decode.h>
#include <cpu/difftest.h>
#include <locale.h>

/* The assembly code of instructions executed is only output to the screen
 * when the number of instructions executed is less than this value.
 * This is useful when you use the `si' command.
 * You can modify this value as you want.
 */
#define MAX_INST_TO_PRINT 10

CPU_state cpu = {}; // 全局 CPU 状态变量，定义在 cpu-exec.c 中，包含寄存器和 PC 等信息

uint64_t g_nr_guest_inst = 0;       // 全局指令计数器.
static uint64_t g_timer = 0;        // unit: us. total time spent in cpu_exec().
static bool g_print_step = false;   // 

void device_update();


/** 
* trace_and_difftest - 包含4个内容: itrace, ftrace, difftest, 扫描watchpoints.
* 全部用ifdef块包裹. 不开启itrace&ftrace, watchpoints, difftest时, 此函数为空.
* 挂载点: 
* 在exec_once()中被调用.
* @param _this: 当前指令的Decode结构体, 包含指令信息和日志缓冲区等.
* @param dnpc: 当前指令执行后的下一条指令地址 (dynamic next pc).
*/
static void trace_and_difftest(Decode *_this, vaddr_t dnpc) {

  /*******************************ITRACE********************************************/
#ifdef CONFIG_ITRACE
  // ---- ITRACE: 生成 logbuf ----
  {
    char *p = _this->logbuf;
    p += snprintf(p, sizeof(_this->logbuf), FMT_WORD ":", _this->pc);
    int ilen = _this->snpc - _this->pc;
    uint8_t *raw = (uint8_t *)&_this->isa.inst.val;
    for (int i = ilen - 1; i >= 0; i--)
      p += snprintf(p, 4, " %02x", raw[i]);
    int ilen_max = MUXDEF(CONFIG_ISA_x86, 8, 4);
    int space_len = (ilen_max - ilen) * 3 + 1;
    if (space_len < 1) space_len = 1;
    memset(p, ' ', space_len); p += space_len;
#ifndef CONFIG_ISA_loongarch32r
    void disassemble(char *str, int size, uint64_t pc, uint8_t *code, int nbyte);
    disassemble(p, _this->logbuf + sizeof(_this->logbuf) - p,
        MUXDEF(CONFIG_ISA_x86, _this->snpc, _this->pc),
        (uint8_t *)&_this->isa.inst.val, ilen);
#else
    p[0] = '\0';
#endif
  }
  // ---- ITRACE: 消费 logbuf ----
#ifdef CONFIG_ITRACE_COND
  if (CONFIG_ITRACE_COND_EXPR) { log_write("%s\n", _this->logbuf); }
#endif
  // g_print_step=true 时处于单步调试模式(si n, n<10), 直接打印到终端.
  if (g_print_step) { puts(_this->logbuf); }
#endif // CONFIG_ITRACE
  
  // 将指令写入 指令环形缓冲区(IRINGBUF)
#ifdef CONFIG_IRINGBUF
  extern void iringbuf_write(const char *logbuf);
  iringbuf_write(_this->logbuf);
#endif

/*******************************FTRACE********************************************/
#ifdef CONFIG_FTRACE
  extern void ftrace_trace(uint32_t pc, uint32_t inst, uint32_t next_pc);
  ftrace_trace((uint32_t)_this->pc, _this->isa.inst.val, (uint32_t)dnpc);
#endif

/*******************************difftest********************************************/
  IFDEF(CONFIG_DIFFTEST, difftest_step(_this->pc, dnpc));

/*******************************watchpoint********************************************/
#ifdef CONFIG_WATCHPOINT
  // 扫描监视点
  extern bool scan_watchpoints();
  if (scan_watchpoints()) {
    nemu_state.state = NEMU_STOP;
  }
#endif
}


/** 
* exec_once, cpu执行一周期, (并执行TRACE和差分测试)
* 传入pc和上下文结构体s, 执行一周期. 核心是调用函数isa_exec_once(s)
* @param s: Decode结构体指针, 包含指令信息和日志缓冲区等.
* @param pc: 当前指令地址.
*/
static void exec_once(Decode *s, vaddr_t pc) {
  s->pc = pc;
  s->snpc = pc; // 初始化 snpc (static next pc) 为当前 pc
  isa_exec_once(s); // 执行指令. 这一步会完成取指, 译码, 执行, 并更新 s->snpc 和 s->dnpc
  cpu.pc = s->dnpc; // 更新全局 CPU 的 PC 为下一条动态指令地址 (dnpc)
  trace_and_difftest(s, s->dnpc); // TRACE和差分测试
}


/** 
* statistic, 用来打印统计信息
* @param return: null
*/
static void statistic() {
  IFNDEF(CONFIG_TARGET_AM, setlocale(LC_NUMERIC, ""));
#define NUMBERIC_FMT MUXDEF(CONFIG_TARGET_AM, "%", "%'") PRIu64
  Log("host time spent = " NUMBERIC_FMT " us", g_timer);
  Log("total guest instructions = " NUMBERIC_FMT, g_nr_guest_inst);
  if (g_timer > 0) Log("simulation frequency = " NUMBERIC_FMT " inst/s", g_nr_guest_inst * 1000000 / g_timer);
  else Log("Finish running in less than 1 us and can not calculate the simulation frequency");
}


/** 
* assert_fail_msg()在断言失败时调用, 用来打印寄存器状态和trace统计信息.
* @param return: null
*/
void assert_fail_msg() {
  isa_reg_display();
  // 打印指令环形缓冲区
#ifdef CONFIG_IRINGBUF
  extern void iringbuf_display();
  iringbuf_display();
#endif
  // FTRACE
#ifdef CONFIG_FTRACE
  extern void ftrace_display();
  ftrace_display();
#endif
  statistic();  //打印统计信息
}

/** 
* 执行n条指令. 其实就是包装了一下exec_once, 并增加了计时和nemu状态机管理.
* 传入pc和上下文结构体s, 执行一周期. 核心是调用函数isa_exec_once(s)
* @param n: 要执行的指令数量.
*/
void cpu_exec(uint64_t n) {
  g_print_step = (n < MAX_INST_TO_PRINT); //打印控制. 若n<10, 打开单步打印模式(打印每一条的反汇编结果)

  //检查当前模拟器状态机状态. 如果是NEMU_END和NEMU_ABORT, 则提示用户需要重启模拟器. 否则将状态机状态设置为NEMU_RUNNING
  switch (nemu_state.state) {
    case NEMU_END: case NEMU_ABORT:
      printf("Program execution has ended. To restart the program, exit NEMU and run again.\n");
      return;
    default: nemu_state.state = NEMU_RUNNING;
  }

  uint64_t timer_start = get_time();  //计时开始.

/**********************执行n条指令****************************/
  Decode s;
  for (;n > 0; n --) {
    exec_once(&s, cpu.pc);  // exec_once 内部已调用 trace_and_difftest
    g_nr_guest_inst ++;
    if (nemu_state.state != NEMU_RUNNING) break;
    IFDEF(CONFIG_DEVICE, device_update());
  }
/***********************************************************/

  //计算总时长g_timer
  uint64_t timer_end = get_time();
  g_timer += timer_end - timer_start;

  //根据运行结果更新状态.
  //如果程序正常执行了n条指令, 此时state应该是RUNNING; 如果程序中途遇到ebreak, halt, 则此时state是END; 如果程序出错, 则此时state=ABORT; 如果用户主动退出, 则state=QUIT.
  switch (nemu_state.state) {
    case NEMU_RUNNING: nemu_state.state = NEMU_STOP; break;//如果还在运行, 则改为END状态.

  //如果是END, ABORT, QUIT状态, 则打印统计信息.
    case NEMU_END: case NEMU_ABORT:
      Log("nemu: %s at pc = " FMT_WORD,
          (nemu_state.state == NEMU_ABORT ? ANSI_FMT("ABORT", ANSI_FG_RED) :
           (nemu_state.halt_ret == 0 ? ANSI_FMT("HIT GOOD TRAP", ANSI_FG_GREEN) :
            ANSI_FMT("HIT BAD TRAP", ANSI_FG_RED))),
          nemu_state.halt_pc);
      
      // 如果是 BAD TRAP 或 ABORT，打印指令环形缓冲区
#ifdef CONFIG_IRINGBUF
      if (nemu_state.state == NEMU_ABORT || nemu_state.halt_ret != 0) {
        extern void iringbuf_display();
        iringbuf_display();
      }
#endif
      // 如果是 BAD TRAP 或 ABORT，打印函数调用追踪
#ifdef CONFIG_FTRACE
      if (nemu_state.state == NEMU_ABORT || nemu_state.halt_ret != 0) {
        extern void ftrace_display();
        ftrace_display();
      }
#endif
      // fall through
    case NEMU_QUIT: statistic();
  }
}
