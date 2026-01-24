/***************************************************************************************
* ============================== ISA 特定的 DiffTest ==============================
*
* 本文件实现 ISA 相关的 DiffTest 功能，特别是寄存器比较。
* 由于不同 ISA 的寄存器集合、命名、编码都不同，需要 ISA 特定实现。
*
* 【文件位置约定】
* - DiffTest 通用框架: nemu/src/cpu/difftest/dut.c
* - ISA 特定实现: nemu/src/isa/$ISA/difftest/dut.c
*   例如:
*   - nemu/src/isa/riscv32/difftest/dut.c (本文件)
*   - nemu/src/isa/x86/difftest/dut.c
*   - nemu/src/isa/mips32/difftest/dut.c
*
* 【本文件的职责】
* 1. 实现 isa_difftest_checkregs() - 比较 ISA 特定的寄存器
* 2. 实现 isa_difftest_attach() - ISA 特定的初始化 (目前为空)
*
* =============================== 寄存器顺序约定 =======================================
*
* DiffTest 要求寄存器必须按特定顺序序列化。对于 RISC-V:
*
* 内存布局 (由 difftest_regcpy 序列化):
*   [0]     x0 (zero register)
*   [1]     x1 (ra - return address)
*   [2]     x2 (sp - stack pointer)
*   ...
*   [31]    x31 (最后一个通用寄存器，或 x15 如果是 RVE)
*   [32]    PC  (程序计数器)
*
* 这个约定在 nemu/include/difftest-def.h 的 DIFFTEST_REG_SIZE 中定义：
*   #define DIFFTEST_REG_SIZE (sizeof(uint32_t) * (32 + 1))
*
* REF (Spike) 必须按此顺序实现 difftest_regcpy()，
* DUT (nemu) 的 CPU_state 结构体也必须满足此约定。
*
* CPU_state 结构体定义在 nemu/src/isa/riscv32/include/isa-def.h:
*   typedef struct {
*     word_t gpr[32];  // x0, x1, ..., x31
*     vaddr_t pc;      // 程序计数器
*   } riscv32_CPU_state;
*
* 幸运的是，C 语言结构体成员的内存排列是顺序的，所以 CPU_state 的内存布局
* 恰好满足约定：gpr[0..31] 然后 pc
*
* ===============================================================================
*
* 关于内存的注意事项：
* - DiffTest 不比较内存内容，只比较寄存器
* - 原因 1: 获取 REF 修改的内存位置开销大
* - 原因 2: 对比整个内存性能损失过大
* - 原因 3: 内存状态差异最终会通过寄存器差异暴露出来 (当指令读取被修改的内存时)
***************************************************************************************/

#include <isa.h>
#include <cpu/difftest.h>
#include "../local-include/reg.h"

// 寄存器数量（RVE 模式为 16 个，标准为 32 个）
#define NR_GPR MUXDEF(CONFIG_RVE, 16, 32)

/* ============================================================================
 * isa_difftest_checkregs(CPU_state *ref_r, vaddr_t pc)
 *
 * 【DiffTest 的核心函数】
 * 比较 DUT 和 REF 的所有通用寄存器和 PC
 * 由 nemu/src/cpu/difftest/dut.c 的 difftest_step() 调用
 *
 * 参数:
 *   ref_r: REF 的寄存器状态 (CPU_state 指针)
 *           通过 ref_difftest_regcpy(&ref_r, DIFFTEST_TO_DUT) 填充
 *   pc: 当前执行的指令 PC (用于错误报告)
 *
 * 返回值:
 *   true:  DUT 和 REF 的寄存器完全一致，继续执行
 *   false: 发现寄存器不一致，DiffTest 会中止 NEMU 执行
 *
 * 作用：
 *   这个函数实现了 DiffTest 的核心验证逻辑：
 *   - 遍历所有通用寄存器，逐个比较
 *   - 比较 PC
 *   - 任何不一致都立即报告并返回 false
 *
 * 关于 CPU_state 的寄存器顺序：
 *   CPU_state.gpr[i] 中的 i 直接对应 RISC-V ISA 的 x 寄存器编号
 *   例如: gpr[1] = x1 (ra), gpr[2] = x2 (sp), ...
 *
 * ============================================================================ */
bool isa_difftest_checkregs(CPU_state *ref_r, vaddr_t pc) {
  // 比较所有通用寄存器
  for (int i = 0; i < NR_GPR; i++) {
    if (ref_r->gpr[i] != cpu.gpr[i]) {
      Log("Difftest FAILED at PC = " FMT_WORD, pc);
      Log("  Register %s (x%d) mismatch:", reg_name(i), i);
      Log("    REF = " FMT_WORD ", DUT = " FMT_WORD, ref_r->gpr[i], cpu.gpr[i]);
      return false;
    }
  }
  
  // 比较 PC
  if (ref_r->pc != cpu.pc) {
    Log("Difftest FAILED at PC = " FMT_WORD, pc);
    Log("  PC mismatch:");
    Log("    REF = " FMT_WORD ", DUT = " FMT_WORD, ref_r->pc, cpu.pc);
    return false;
  }
  
  // 所有寄存器都一致
  return true;
}

/* ============================================================================
 * isa_difftest_attach()
 *
 * 【ISA 特定的初始化函数】
 * 用于 ISA 特定的初始化，目前为空
 * 将来可以用于：
 *   - 初始化 ISA 特定的 DiffTest 状态
 *   - 设置寄存器别名或映射
 *   - 其他 ISA 特定的准备工作
 * ============================================================================ */
void isa_difftest_attach() {
  // RISC-V 目前不需要特殊的初始化
}
