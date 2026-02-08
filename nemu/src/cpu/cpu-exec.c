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

uint64_t g_nr_guest_inst = 0; // 全局指令计数器.
static uint64_t g_timer = 0; // unit: us. total time spent in cpu_exec().
static bool g_print_step = false;

void device_update();


//================= trace_and_difftest 相关 =====================
static void trace_and_difftest(Decode *_this, vaddr_t dnpc) {
#ifdef CONFIG_ITRACE_COND
  if (CONFIG_ITRACE_COND_EXPR) { log_write("%s\n", _this->logbuf); }
#endif
  if (g_print_step) { IFDEF(CONFIG_ITRACE, puts(_this->logbuf)); }
  
  // 将指令写入环形缓冲区
#ifdef CONFIG_IRINGBUF
  extern void iringbuf_write(const char *logbuf);
  iringbuf_write(_this->logbuf);
#endif

  IFDEF(CONFIG_DIFFTEST, difftest_step(_this->pc, dnpc));
  
#ifdef CONFIG_WATCHPOINT
  // 扫描监视点
  extern bool scan_watchpoints();
  if (scan_watchpoints()) {
    nemu_state.state = NEMU_STOP;
  }
#endif
}

// 传入pc和上下文结构体s, 执行一周期. 核心是调用函数isa_exec_once(s)
static void exec_once(Decode *s, vaddr_t pc) {
  s->pc = pc;
  s->snpc = pc; // 初始化 snpc (static next pc) 为当前 pc
  isa_exec_once(s); // 执行指令. 这一步会完成取指, 译码, 执行, 并更新 s->snpc 和 s->dnpc
  cpu.pc = s->dnpc; // 更新全局 CPU 的 PC 为下一条动态指令地址 (dnpc)

#ifdef CONFIG_ITRACE
  // 如果开启了指令追踪 (ITRACE), 则记录指令的执行日志
  char *p = s->logbuf;
  // 1. 记录 PC 地址
  p += snprintf(p, sizeof(s->logbuf), FMT_WORD ":", s->pc);
  
  // 2. 记录指令的机器码
  int ilen = s->snpc - s->pc; // 计算指令长度
  int i;
  uint8_t *inst = (uint8_t *)&s->isa.inst.val;
  for (i = ilen - 1; i >= 0; i --) {
    p += snprintf(p, 4, " %02x", inst[i]); // 逐字节打印机器码
  }
  
  // 3. 格式化对齐 (为了日志美观)
  int ilen_max = MUXDEF(CONFIG_ISA_x86, 8, 4);
  int space_len = ilen_max - ilen;
  if (space_len < 0) space_len = 0;
  space_len = space_len * 3 + 1;
  memset(p, ' ', space_len);
  p += space_len;

  // 4. 记录反汇编结果
#ifndef CONFIG_ISA_loongarch32r
  void disassemble(char *str, int size, uint64_t pc, uint8_t *code, int nbyte);
  disassemble(p, s->logbuf + sizeof(s->logbuf) - p,
      MUXDEF(CONFIG_ISA_x86, s->snpc, s->pc), (uint8_t *)&s->isa.inst.val, ilen);
#else
  p[0] = '\0'; // the upstream llvm does not support loongarch32r
#endif
#endif
}

// nemu的核心执行函数: 执行n条指令.
static void execute(uint64_t n) {
  Decode s;
  for (;n > 0; n --) {
    exec_once(&s, cpu.pc);
    g_nr_guest_inst ++;
    trace_and_difftest(&s, cpu.pc);
    if (nemu_state.state != NEMU_RUNNING) break;
    IFDEF(CONFIG_DEVICE, device_update());
  }
}

// statistic()用来打印统计信息
static void statistic() {
  IFNDEF(CONFIG_TARGET_AM, setlocale(LC_NUMERIC, ""));
#define NUMBERIC_FMT MUXDEF(CONFIG_TARGET_AM, "%", "%'") PRIu64
  Log("host time spent = " NUMBERIC_FMT " us", g_timer);
  Log("total guest instructions = " NUMBERIC_FMT, g_nr_guest_inst);
  if (g_timer > 0) Log("simulation frequency = " NUMBERIC_FMT " inst/s", g_nr_guest_inst * 1000000 / g_timer);
  else Log("Finish running in less than 1 us and can not calculate the simulation frequency");
}

// assert_fail_msg()在断言失败时调用, 用来打印寄存器状态和统计信息.
void assert_fail_msg() {
  isa_reg_display();
  
  // 打印指令环形缓冲区
#ifdef CONFIG_IRINGBUF
  extern void iringbuf_display();
  iringbuf_display();
#endif

  statistic();
}


/* Simulate how the CPU works. */
//执行n条指令. 其实就是包装了一下execute(n)函数, 增加了计时和nemu状态机管理.
void cpu_exec(uint64_t n) {
  g_print_step = (n < MAX_INST_TO_PRINT); //打印控制. 若n<10, 打开单步打印模式(打印每一条的反汇编结果)
  //检查当前模拟器状态机状态. 如果是NEMU_END和NEMU_ABORT, 则提示用户需要重启模拟器. 否则将状态机状态设置为NEMU_RUNNING
  switch (nemu_state.state) {
    case NEMU_END: case NEMU_ABORT:
      printf("Program execution has ended. To restart the program, exit NEMU and run again.\n");
      return;
    default: nemu_state.state = NEMU_RUNNING;
  }

  //计时开始.
  uint64_t timer_start = get_time();

  //核心. 执行n条指令
  execute(n);

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
      // fall through
    case NEMU_QUIT: statistic();
  }
}
