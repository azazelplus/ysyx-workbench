/***************************************************************************************
* ============================== DiffTest 框架说明 ==============================
* 
* DiffTest 是一种差分测试框架，用于验证 DUT (Device Under Test) 的正确性。
* 原理：通过对比 DUT 和 REF (Reference) 的执行结果来检测错误。
*
* 【角色定义】
* 1. DUT (Device Under Test) - 被测试的模拟器
*    - 本文件 (nemu/src/cpu/difftest/dut.c) 是 DUT 端的框架代码
*    - DUT 需要调用 REF 提供的 API 接口
*
* 2. REF (Reference Implementation) - 参考实现
*    - 可以是 Spike、QEMU、KVM 等其他模拟器
*    - 编译为动态库 (.so)，暴露标准 API 接口给 DUT 调用
*    - API 实现在 nemu/tools/spike-diff/difftest.cc、nemu/tools/qemu-diff/ 等
*
* 【REF 必须实现的 5 个 API 接口】(在 REF 的 .so 中)
* 1. void difftest_init(int port)
*    - 功能: 初始化 REF 的 DiffTest 环境
*    - DUT 调用时机: init_difftest() 中
*
* 2. void difftest_memcpy(paddr_t addr, void *buf, size_t n, bool direction)
*    - 功能: 在 DUT 和 REF 的内存之间拷贝数据
*    - direction = DIFFTEST_TO_REF: 从 DUT 内存 → REF 内存
*    - direction = DIFFTEST_TO_DUT: 从 REF 内存 → DUT 内存 (通常不用)
*    - DUT 调用时机: init_difftest() 初始化内存
*
* 3. void difftest_regcpy(void *dut, bool direction)
*    - 功能: 在 DUT 和 REF 的寄存器之间拷贝数据
*    - direction = DIFFTEST_TO_REF: 从 dut 结构体 → REF 寄存器
*    - direction = DIFFTEST_TO_DUT: 从 REF 寄存器 → dut 结构体
*    - dut 必须按 DIFFTEST_REG_SIZE 约定的顺序排列寄存器 (gpr[0..31] + pc)
*
* 4. void difftest_exec(uint64_t n)
*    - 功能: 让 REF 执行 n 条指令
*    - DUT 调用时机: difftest_step() 中，每执行 1 条 DUT 指令后，让 REF 也执行 1 条
*
* 5. void difftest_raise_intr(uint64_t NO)
*    - 功能: 在 REF 中引发中断/异常
*    - DUT 调用时机: 当 DUT 触发中断时
*
* =============================== 数据流向 =======================================
*
*   DUT (nemu) 执行流程:
*   ┌─────────────────────────────┐
*   │  cpu_exec() 主循环          │
*   │  ├─ fetch/decode/execute    │
*   │  │  (执行 1 条 DUT 指令)     │
*   │  └─ difftest_step()         │ ◄─── DUT 每执行一条指令就调用此函数
*   │     ├─ ref_difftest_exec(1) │ ◄─── 调用 REF 的 API，让 REF 也执行 1 条
*   │     │  └─ [Spike 执行 1 条]  │
*   │     ├─ ref_difftest_regcpy()│ ◄─── 读出 REF 的寄存器状态
*   │     │  └─ [REF.gpr → ref_r]│
*   │     └─ isa_difftest_checkregs()
*   │        ├─ DUT.gpr vs REF.gpr  ◄─── 比较两者是否一致
*   │        └─ DUT.pc vs REF.pc    ◄─── ISA 特定的比较逻辑
*   └─────────────────────────────┘
*
* =============================== 关键设计 =======================================
* - nemu/src/cpu/difftest/dut.c: 通用框架 (ISA 无关的加载、流程控制)
* - nemu/src/isa/riscv32/difftest/dut.c: ISA 特定 (只有 isa_difftest_checkregs)
*
* 【为什么用动态链接?】
* - 使 DUT 可以灵活选择不同的 REF (Spike/QEMU/KVM)
* - REF 只需实现标准 API，无需修改 DUT 代码
*
* 【寄存器顺序约定】
* difftest-def.h 中定义了 DIFFTEST_REG_SIZE，对于 RISC-V:
*   struct {
*     word_t gpr[32];  // 或 16 (RVE)
*     word_t pc;
*   }
* REF 的 difftest_regcpy() 必须按此顺序序列化寄存器
*
* =============================== 扩展点 =======================================
*
* difftest_skip_ref()   - 用于跳过某些无法在 REF 中重现的指令
* difftest_skip_dut()   - 用于处理 QEMU 的"指令打包"问题
*
***************************************************************************************/

#include <dlfcn.h>

#include <isa.h>
#include <cpu/cpu.h>
#include <memory/paddr.h>
#include <utils.h>
#include <difftest-def.h>

/* ============================================================================
 * REF 的 API 函数指针
 * 这些指针在 init_difftest() 中通过 dlsym() 从 REF .so 中动态解析
 * ============================================================================ */

// 从 REF 中读出内存/寄存器，或向 REF 写入内存/寄存器
void (*ref_difftest_memcpy)(paddr_t addr, void *buf, size_t n, bool direction) = NULL;
void (*ref_difftest_regcpy)(void *dut, bool direction) = NULL;

// 让 REF 执行 n 条指令
void (*ref_difftest_exec)(uint64_t n) = NULL;

// 在 REF 中触发中断
void (*ref_difftest_raise_intr)(uint64_t NO) = NULL;

#ifdef CONFIG_DIFFTEST

/* ============================================================================
 * DiffTest 的状态管理
 * ============================================================================ */

// 标志：是否要跳过当前指令在 REF 中的执行
// 某些指令（如 nemu_trap）无法在 REF 中正确执行，需要跳过
static bool is_skip_ref = false;

// 累计待跳过的 DUT 指令数
// 用于处理 QEMU 的"指令打包"：QEMU 可能一次执行多条指令
// 此时 DUT 需要多条指令来追上 REF 的 PC
static int skip_dut_nr_inst = 0;

/* ============================================================================
 * difftest_skip_ref()
 * 用途: 告诉 DiffTest 框架跳过当前指令在 REF 中的执行
 * 原因: 某些指令在 REF 中无法执行或执行结果必定不同
 *       例如: nemu_trap (NEMU 自定义的陷阱指令)
 * 效果: difftest_step() 中会直接将 DUT 的寄存器同步到 REF，跳过执行比较
 * ============================================================================ */
void difftest_skip_ref() {
  is_skip_ref = true;
  // 重置累计跳过计数，因为我们已经处理了指令打包问题
  skip_dut_nr_inst = 0;
}

/* ============================================================================
 * difftest_skip_dut(int nr_ref, int nr_dut)
 * 用途: 处理 REF (如 QEMU) 的"指令打包"问题
 * 背景: QEMU 有时会把多条指令合并执行 (为了性能优化)
 *       例如: difftest_exec(1) 在 QEMU 中执行了 3 条指令
 *       此时 REF.pc = DUT.pc + 12 (3 条指令的字节长度)
 * 参数:
 *       nr_ref: REF 已经额外执行的指令数 (通常不需要我们让 REF 再执行)
 *       nr_dut: DUT 期望追上 REF PC 的最大指令数
 * 效果: 立即让 REF 执行 nr_ref 条指令，然后对后续 nr_dut 条 DUT 指令跳过检查
 * ============================================================================ */
void difftest_skip_dut(int nr_ref, int nr_dut) {
  skip_dut_nr_inst += nr_dut;

  // 先让 REF 执行那些被打包的指令
  while (nr_ref -- > 0) {
    ref_difftest_exec(1);
  }
}

/* ============================================================================
 * init_difftest(char *ref_so_file, long img_size, int port)
 *
 * 这是 DiffTest 的初始化函数，由 NEMU 监控器在启动时调用
 * 职责:
 *   1. 动态加载 REF 的 .so 文件
 *   2. 通过 dlsym() 解析 REF 提供的 5 个 API 符号
 *   3. 初始化 REF 的内部状态
 *   4. 同步内存: DUT memory → REF memory
 *   5. 同步寄存器: DUT registers → REF registers
 *   完成后，DUT 和 REF 处于完全相同的状态，可以开始逐条指令的比较
 *
 * 参数:
 *   ref_so_file: REF 动态库的路径，由 Makefile 通过 --diff 选项传入
 *   img_size: 加载的程序镜像大小 (字节)
 *   port: 保留参数，某些 REF (如 QEMU) 可能使用来建立通信端口
 *
 * 典型调用栈:
 *   main() → nemu_state.state = RUNNING
 *   monitor() → parse_args(argc, argv) 
 *   parse_args() → init_difftest(ref_so_file, img_size, port)
 * ============================================================================ */
void init_difftest(char *ref_so_file, long img_size, int port) {
  assert(ref_so_file != NULL);

  /* 步骤 1: 动态加载 REF 的 .so 文件 */
  void *handle;
  handle = dlopen(ref_so_file, RTLD_LAZY);  // RTLD_LAZY: 延迟符号解析
  assert(handle);

  /* 步骤 2: 通过 dlsym() 从 .so 中解析 API 符号
   * 这是一个契约：REF 必须导出这 5 个符号，否则 dlsym 返回 NULL */

  // API 1: 内存拷贝
  ref_difftest_memcpy = dlsym(handle, "difftest_memcpy");
  assert(ref_difftest_memcpy);

  // API 2: 寄存器拷贝
  ref_difftest_regcpy = dlsym(handle, "difftest_regcpy");
  assert(ref_difftest_regcpy);

  // API 3: 执行指令
  ref_difftest_exec = dlsym(handle, "difftest_exec");
  assert(ref_difftest_exec);

  // API 4: 触发中断 (某些 REF 可能不实现)
  ref_difftest_raise_intr = dlsym(handle, "difftest_raise_intr");
  assert(ref_difftest_raise_intr);

  // API 5: 初始化 REF
  void (*ref_difftest_init)(int) = dlsym(handle, "difftest_init");
  assert(ref_difftest_init);

  /* 步骤 3: 初始化 REF */
  Log("Differential testing: %s", ANSI_FMT("ON", ANSI_FG_GREEN));
  Log("The result of every instruction will be compared with %s. "
      "This will help you a lot for debugging, but also significantly reduce the performance. "
      "If it is not necessary, you can turn it off in menuconfig.", ref_so_file);

  ref_difftest_init(port);

  /* 步骤 4: 同步内存 (DUT → REF)
   * 将程序镜像从 DUT 的内存复制到 REF 的内存中
   * RESET_VECTOR 是程序的起始地址 (通常 0x80000000 for RISC-V)
   * guest_to_host() 将客户机物理地址转换为主机虚拟地址 */
  ref_difftest_memcpy(RESET_VECTOR, guest_to_host(RESET_VECTOR), img_size, DIFFTEST_TO_REF);

  /* 步骤 5: 同步寄存器 (DUT → REF)
   * 将 DUT 当前的寄存器状态（初始化后的）复制到 REF 中
   * &cpu 是 DUT 的 CPU_state 结构体指针，包含 gpr[32] 和 pc */
  ref_difftest_regcpy(&cpu, DIFFTEST_TO_REF);
}

/* ============================================================================
 * checkregs(CPU_state *ref, vaddr_t pc)
 * 
 * 内部辅助函数，调用 ISA 特定的寄存器比较函数
 * 如果比较失败，设置 NEMU 状态为 ABORT，记录失败 PC
 * 
 * 参数:
 *   ref: REF 的寄存器状态
 *   pc: 当前执行指令的 PC（用于错误报告）
 * ============================================================================ */
static void checkregs(CPU_state *ref, vaddr_t pc) {
  // 调用 ISA 特定的比较函数 (在 nemu/src/isa/riscv32/difftest/dut.c 中实现)
  if (!isa_difftest_checkregs(ref, pc)) {
    // 比较失败：设置 NEMU 状态为中止，并保存失败的 PC
    nemu_state.state = NEMU_ABORT;
    nemu_state.halt_pc = pc;
    // 打印 DUT 的寄存器状态供调试
    isa_reg_display();
  }
}

/* ============================================================================
 * difftest_step(vaddr_t pc, vaddr_t npc)
 *
 * DiffTest 的核心函数，在 cpu_exec() 主循环中每执行一条 DUT 指令后调用
 * 职责：
 *   1. 让 REF 执行相应的指令 (如果不跳过)
 *   2. 读出 REF 的寄存器状态
 *   3. 调用 isa_difftest_checkregs() 进行比较
 *   如果不一致，停止 NEMU 执行并报错
 *
 * 参数:
 *   pc: 当前 DUT 执行的指令的 PC
 *   npc: 执行此指令后 DUT 的下一条指令 PC (用于跳过检查时比较)
 *
 * 调用时机：
 *   cpu_exec() 每执行完一条指令后，在 fetch_decode_exec_updatepc() 返回后
 *
 * 状态机：
 *   normal → 正常比较模式 (默认)
 *   skip_dut → 跳过 DUT 指令检查的模式 (处理指令打包)
 *   skip_ref → 跳过 REF 执行的模式 (处理特殊指令)
 * ============================================================================ */
void difftest_step(vaddr_t pc, vaddr_t npc) {
  // REF 的寄存器状态缓冲区
  CPU_state ref_r;

  /* 情况 1: 处理指令打包
   * 当 skip_dut_nr_inst > 0 时，我们期望 DUT 多条指令来追上 REF 的 PC
   * 此时跳过这些指令的检查 */
  if (skip_dut_nr_inst > 0) {
    // 读出 REF 当前的寄存器状态
    ref_difftest_regcpy(&ref_r, DIFFTEST_TO_DUT);
    
    // 如果 DUT 已经追上 REF，说明指令打包问题已解决
    if (ref_r.pc == npc) {
      skip_dut_nr_inst = 0;  // 重置计数器
      checkregs(&ref_r, npc);  // 进行最后一次比较
      return;
    }
    
    // 否则，继续跳过此指令的检查，减少待跳过计数
    skip_dut_nr_inst --;
    
    // 如果计数器到 0 但 PC 还是没追上，说明出错
    if (skip_dut_nr_inst == 0)
      panic("can not catch up with ref.pc = " FMT_WORD " at pc = " FMT_WORD, ref_r.pc, pc);
    return;
  }

  /* 情况 2: 跳过当前指令在 REF 中的执行
   * 对于某些无法在 REF 中正确执行的指令（如 nemu_trap）
   * 我们跳过 REF 的执行，直接将 DUT 的寄存器同步到 REF */
  if (is_skip_ref) {
    // 将 DUT 的寄存器状态直接复制到 REF (覆盖 REF 的状态)
    // 这相当于："该指令的执行结果以 DUT 为准"
    ref_difftest_regcpy(&cpu, DIFFTEST_TO_REF);
    is_skip_ref = false;  // 处理完此指令，重置标志
    return;
  }

  /* 情况 3: 正常模式 - 比较 DUT 和 REF
   * 这是 DiffTest 的核心逻辑 */
  
  // 步骤 1: 让 REF 执行 1 条指令
  ref_difftest_exec(1);
  
  // 步骤 2: 读出 REF 执行后的寄存器状态到 ref_r
  ref_difftest_regcpy(&ref_r, DIFFTEST_TO_DUT);
  
  // 步骤 3: 比较 DUT 和 REF 的寄存器状态
  checkregs(&ref_r, pc);
}
#else
void init_difftest(char *ref_so_file, long img_size, int port) { }
#endif
