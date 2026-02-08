/***************************************************************************************
* nemu的interruption 实现.
***************************************************************************************/

#include <isa.h>

// RISC-V 异常/中断原因码描述表
static const char *exception_names[] = {
  [0]  = "Instruction address misaligned",
  [1]  = "Instruction access fault",
  [2]  = "Illegal instruction",
  [3]  = "Breakpoint",
  [4]  = "Load address misaligned",
  [5]  = "Load access fault",
  [6]  = "Store/AMO address misaligned",
  [7]  = "Store/AMO access fault",
  [8]  = "Environment call from U-mode",
  [9]  = "Environment call from S-mode",
  [11] = "Environment call from M-mode",
  [12] = "Instruction page fault",
  [13] = "Load page fault",
  [15] = "Store/AMO page fault",
};

static const char *interrupt_names[] = {
  [7]  = "Machine timer interrupt",
  [11] = "Machine external interrupt",
};

/**
 * isa_raise_intr - 使nemu触发异常/中断. nemu的ecall指令所做的就是调用这个函数, 并把返回值mtvec存在dnpc, 准备跳转. 
 * isa_raise_intr具体做的事: 写mepc, mcause, mstatus寄存器, 并返回mtvec寄存器的值.
 * @param NO:  异常原因号 (写入 mcause, 如 11 = M-mode ecall, 3 = breakpoint)
 * @param epc: 异常发生时的 PC (写入 mepc)
 * @return:    异常入口地址 (mtvec 的值)
 * 3. mstatus: MIE -> MPIE, MIE <- 0, MPP <- 当前特权级(M)
 * 4. PC <- mtvec          (跳转到异常处理入口)
 */
word_t isa_raise_intr(word_t NO, vaddr_t epc) {
  cpu.mepc = epc;   // 写mepc寄存器, 保存案发现场 PC
  cpu.mcause = NO;  // 写mcause寄存器, 记录异常原因

  // mstatus 处理: 备份 MIE 到 MPIE, 关闭 MIE, 设置 MPP=11(M-mode)
  word_t mie = (cpu.mstatus >> 3) & 1;
  cpu.mstatus = (cpu.mstatus & ~(1 << 7)) | (mie << 7);  // MPIE = MIE
  cpu.mstatus &= ~(1 << 3);                                // MIE = 0
  cpu.mstatus = (cpu.mstatus & ~(3 << 11)) | (3 << 11);   // MPP = 11 (M-mode)


  //Exception trace: 记录异常/中断事件
#ifdef CONFIG_ETRACE
  // Exception trace: 记录异常/中断事件
  bool is_interrupt = (NO & (1ULL << 63)) != 0;
  word_t cause = NO & ~(1ULL << 63);
  const char *event_type = is_interrupt ? "interrupt" : "exception";
  const char *event_name = NULL;
  
  if (is_interrupt && cause < sizeof(interrupt_names)/sizeof(interrupt_names[0])) {
    event_name = interrupt_names[cause];
  } else if (!is_interrupt && cause < sizeof(exception_names)/sizeof(exception_names[0])) {
    event_name = exception_names[cause];
  }
  
  if (event_name) {
    log_write("[ETRACE] %s #%d (%s) @ " FMT_WORD ", mtvec=" FMT_WORD "\n",
              event_type, cause, event_name, epc, cpu.mtvec);
  } else {
    log_write("[ETRACE] %s #%d (unknown) @ " FMT_WORD ", mtvec=" FMT_WORD "\n",
              event_type, cause, epc, cpu.mtvec);
  }
#endif

  return cpu.mtvec; // 返回mtvec寄存器的值)
}

word_t isa_query_intr() {
  return INTR_EMPTY;
}
