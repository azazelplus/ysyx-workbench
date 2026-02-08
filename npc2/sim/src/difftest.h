/***************************************************************************************
 * difftest.h - NPC2 DiffTest (差分测试) 头文件
 * 
 * 【概述】
 * DiffTest 通过对比 DUT (npc2) 和 REF (nemu) 的执行结果来检测 CPU 实现错误。
 * 
 * 【架构角色】
 * - npc2: DUT (Device Under Test) - 被测试的 CPU 硬件仿真
 * - nemu: REF (Reference Implementation) - 参考软件模拟器，编译为 .so
 * 
 * 【工作流程】
 * 1. init_difftest(): 加载 nemu.so，同步初始状态（内存、寄存器）
 * 2. 主循环中每执行一条指令后调用 difftest_step()
 * 3. difftest_step(): 
 *    - 让 REF 执行 1 条指令
 *    - 读取 REF 寄存器状态
 *    - 与 DUT 寄存器比较
 *    - 不一致则报错并终止
 * 
 * 【编译开关】
 * 在 config.h 中设置 ENABLE_DIFFTEST 来启用/禁用此功能
 * 
 ***************************************************************************************/

#ifndef __DIFFTEST_H__
#define __DIFFTEST_H__

#include <cstdint>
#include <cstddef>

// ============ REF 动态库路径 ============
// 默认使用 nemu 作为 REF，编译为动态库
// 路径相对于 ysyx-workbench 根目录
#ifndef DIFFTEST_REF_SO
#define DIFFTEST_REF_SO "nemu/build/riscv32-nemu-interpreter-so"
#endif

// ============ 常量定义 ============
// 存储器基地址
#define DIFFTEST_MEM_BASE 0x80000000UL

// 寄存器传输方向
enum { DIFFTEST_TO_DUT, DIFFTEST_TO_REF };

// ================ API 声明 ================

/**
 * init_difftest - 初始化 DiffTest 框架
 * 
 * @param ref_so_file: REF 动态库路径 (nemu.so)
 * @param img_size:    程序镜像大小（字节）
 * @param mem:         DUT 内存指针（程序已加载）
 * 
 * 功能：
 * 1. 使用 dlopen 加载 REF 动态库 (即nemu/build/riscv32-nemu-interpreter-so)
 * 2. 使用 dlsym 获取 5 个 API 函数指针 ()
 * 3. 调用 ref_difftest_init() 初始化 REF
 * 4. 调用 ref_difftest_memcpy() 将程序镜像同步到 REF
 * 5. 调用 ref_difftest_regcpy() 将初始寄存器状态同步到 REF
 */
void init_difftest(const char *ref_so_file, long img_size, uint8_t *mem);


/**
 * difftest_step - 执行一步差分测试
 * 
 * @param pc:   当前指令的 PC
 * @param regs: DUT 当前的 32 个通用寄存器值
 * 
 * @return: true = 比较通过，false = 检测到错误
 * 
 * 功能：
 * 1. 让 REF 执行 1 条指令
 * 2. 读取 REF 的寄存器状态
 * 3. 比较 DUT 和 REF 的所有寄存器及 PC
 * 4. 不一致时打印详细错误信息
 */
bool difftest_step(uint32_t pc, uint32_t *regs);


/**
 * difftest_raise_intr - 让 REF 触发中断/异常
 *
 * @param NO: 中断/异常号
 *
 * 功能：调用 REF 的 difftest_raise_intr()，让 nemu 更新异常相关状态。
 */
void difftest_raise_intr(uint64_t NO);


/**
 * difftest_skip_ref - 跳过 REF 执行
 * 
 * 用于处理特殊指令（如 ebreak），这些指令在 REF 中可能有不同行为。
 * 调用后，下一次 difftest_step() 会跳过 REF 执行，直接同步 DUT 状态到 REF。
 */
void difftest_skip_ref();

#endif /* __DIFFTEST_H__ */
