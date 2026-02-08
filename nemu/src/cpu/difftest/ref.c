/***************************************************************************************
* ============================== nemu 用作 REF 端 DiffTest 实现 ==============================
* 
* 本文件实现 nemu 作为 REF (Reference Implementation) 时暴露给 DUT 的 5 个标准 API。
* 当 nemu 被编译为动态库 .so 时，这些函数会被 DUT (如 npc2) 通过 dlsym(动态链接器) 加载和调用。
* 
* 【编译方式】
* 在 menuconfig 中选择 "Build target" -> "Shared object (used as REF for differential testing)"
* 即设置 CONFIG_TARGET_SHARE=y，然后编译生成 riscv32-nemu-interpreter-so.
* 然后在npc2端打开difftest宏, 然后make ARCH=riscv32-npc2 运行.
* 
* 【API 接口说明】
* 1. difftest_init(port)       - 初始化 REF 环境（内存、ISA 状态）
* 2. difftest_memcpy(...)      - DUT 与 REF 之间的内存拷贝
* 3. difftest_regcpy(...)      - DUT 与 REF 之间的寄存器拷贝
* 4. difftest_exec(n)          - 让 REF 执行 n 条指令
* 5. difftest_raise_intr(NO)   - 在 REF 中触发中断
* 
* 【寄存器布局约定】(RISC-V 32)
* DUT 传入的 dut 缓冲区必须按以下顺序排列（共 33 个 uint32_t）:
*   dut[0..31]  = gpr[0..31]  (通用寄存器 x0 ~ x31)
*   dut[32]     = pc          (程序计数器)
* 
***************************************************************************************/

#include <isa.h>
#include <cpu/cpu.h>
#include <difftest-def.h>
#include <memory/paddr.h>



/***************************************************************************************
* difftest_init - 初始化 REF 的 DiffTest 环境
* @param port: 调试端口（目前未使用，保留给 GDB 远程调试等功能）
* 【初始化内容】
* 1. init_mem(): 初始化 REF 的物理内存
* 2. init_isa(): 初始化 ISA 相关状态（寄存器初始值、PC 初始值等）
* 【调用时机】
* - DUT 的 init_difftest() 中，通过 dlsym 获取此函数后立即调用
***************************************************************************************/
__EXPORT void difftest_init(int port) {
  init_mem();  //初始化nemu内存
  /* Perform ISA dependent initialization. */
  init_isa(); //初始化nemu寄存器
}





/***************************************************************************************
* difftest_memcpy - 在 DUT 和 REF 的内存之间拷贝数据
* 
* @param addr:      目标/源物理地址 (客户机(即测试程序运行的机器, 即npc2)地址空间)
* @param buf:       主机侧缓冲区. 在wsl2所拥有的内存空间开辟的数组.
* @param n:         要拷贝的字节数
* @param direction: DIFFTEST_TO_REF = DUT → REF (buf 写入 REF 内存)
*                   DIFFTEST_TO_DUT = REF → DUT (REF 内存读取到 buf)
* 
* 【调用时机】
* - init_difftest() 时，DUT 调用此函数将程序镜像拷贝到 REF 内存
* - 特殊指令（如 CSR 操作修改内存映射）后需要同步
***************************************************************************************/
__EXPORT void difftest_memcpy(paddr_t addr, void *buf, size_t n, bool direction) {
  if (direction == DIFFTEST_TO_REF) {
    // DUT → REF: 将 buf 的内容写入 REF 的物理内存
    memcpy(guest_to_host(addr), buf, n);
  } else {
    // REF → DUT: 从 REF 的物理内存读取到 buf（通常不用）
    memcpy(buf, guest_to_host(addr), n);
  }
}



/***************************************************************************************
* difftest_regcpy - 在 DUT 和 REF 的寄存器之间拷贝数据
* @param dut:       长度为 33 的 DUT 寄存器缓冲区数组 (必须按约定格式排列)
* @param direction: DIFFTEST_TO_REF = DUT → REF (将 dut 的值写入 REF 寄存器)
*                   DIFFTEST_TO_DUT = REF → DUT (将 REF 寄存器读取到 dut)
* 【全局变量 cpu】
* cpu 是在 nemu/src/cpu/cpu-exec.c 中定义的全局变量：
*   CPU_state cpu = {};
* 它通过在 nemu/include/isa.h 中的声明对所有文件可见：
*   extern CPU_state cpu;
* 
* CPU_state 的定义（RISC-V）：
*   typedef struct {
*       word_t gpr[32];   // 32 个通用寄存器（或 16 个 RVE）
*       vaddr_t pc;       // 程序计数器
*   } riscv32_CPU_state;
* 
* 【调用时机】
* - init_difftest(): 调用 difftest_regcpy(dut, DIFFTEST_TO_REF)
*   将 DUT 初始状态从 dut 缓冲区同步到 nemu 的 cpu 结构体
* 
* - difftest_step(): 调用 difftest_regcpy(ref_r, DIFFTEST_TO_DUT)
*   将 nemu 的 cpu 结构体（执行后的状态）读取到 ref_r 缓冲区，供 DUT 比较
* 
* 【作用】
* 作为 REF 和 DUT 之间的寄存器状态桥梁。通过这个函数，DiffTest 可以：
* 1. 同步 DUT 的初始状态到 REF（这样两者起点相同）
* 2. 读取 REF 执行后的状态（这样 DUT 可以对比自己的结果）
* =================================================================================*/
__EXPORT void difftest_regcpy(void *dut, bool direction) {
  if (direction == DIFFTEST_TO_REF) {
    // DUT → REF: 将 dut数组(期望存有DUT的寄存器状态.)中的内容写入nemu的寄存器(全局变量cpu). 
    // 这样 nemu 就与 DUT 处于相同的寄存器状态，可以开始执行指令了
    memcpy(&cpu, dut, DIFFTEST_REG_SIZE);
  } else {
    // REF → DUT: 将 nemu 的全局 cpu 结构体（已执行后的状态）读取到 dut 缓冲区
    // DUT 会用这个状态与自己的寄存器状态进行比较
    memcpy(dut, &cpu, DIFFTEST_REG_SIZE);
  }
}



/***************************************************************************************
* difftest_exec - 让 REF 执行 n 条指令
* @param n: 要执行的指令数量
* 【实现细节】
* 直接调用 nemu 的 cpu_exec() 函数，它会执行 n 条指令并更新 cpu 状态
* 【调用时机】
* - difftest_step(): DUT 每执行一条指令后，调用 ref_difftest_exec(1)
*   让 REF 也执行一条指令，然后比较两者的状态
***************************************************************************************/
__EXPORT void difftest_exec(uint64_t n) {
  cpu_exec(n);
}
               


/***************************************************************************************
* difftest_raise_intr - 在 REF 中触发中断
* 
* @param NO: 中断/异常号
* 
* 【实现说明】
* 目前 npc2 不支持中断，此函数暂时不实现
* 当 npc2 添加中断支持时，需要调用 nemu 的中断处理逻辑
* 
* 【调用时机】
* - 当 DUT 触发中断时，需要同步让 REF 也触发相同的中断
*   这样两者才能保持状态一致
***************************************************************************************/
__EXPORT void difftest_raise_intr(word_t NO) {
  // 触发 REF 端的异常/中断，并更新 PC 到异常入口
  cpu.pc = isa_raise_intr(NO, cpu.pc);
}


