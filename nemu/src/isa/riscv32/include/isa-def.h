/***************************************************************************************
* nemu的ISA相关定义.
***************************************************************************************/

#ifndef __ISA_RISCV_H__
#define __ISA_RISCV_H__

#include <common.h>



// 声明CPU_state结构体. 它存储:
// 1.通用寄存器数组gpr[].
// 2.PC寄存器pc.
// 3.CSR寄存器 (mtvec, mepc, mcause, mstatus).
//使用了MUXDEF选择器宏. 如果宏`CONFIG_RV64`被定义了, 就声明结构体名为riscv64_CPU_state, 否则为riscv32_CPU_state.
typedef struct {
  word_t gpr[MUXDEF(CONFIG_RVE, 16, 32)];
  vaddr_t pc;

  // CSR 寄存器 (呃我觉得暂时不需要CSR参与difftest? 现有 difftest 的 memcpy 布局以后有需要得保证没问题)
  word_t mstatus;   // 0x300  Machine Status
  word_t mtvec;     // 0x305  Machine Trap-Vector Base-Address
  word_t mepc;      // 0x341  Machine Exception Program Counter
  word_t mcause;    // 0x342  Machine Cause
} MUXDEF(CONFIG_RV64, riscv64_CPU_state, riscv32_CPU_state);


//声明ISADecodeInfo结构体. 
// val保存当前指令的二进制编码.
// decode
typedef struct {
  union {
    uint32_t val;
  } inst;
} MUXDEF(CONFIG_RV64, riscv64_ISADecodeInfo, riscv32_ISADecodeInfo);

#define isa_mmu_check(vaddr, len, type) (MMU_DIRECT)

#endif
