/***************************************************************************************
 * dpic.h - DPI-C 函数接口声明
 *
 * DPI-C (Direct Programming Interface - C) 函数的声明，这些函数由 SystemVerilog
 * 代码通过 DPI-C 调用，实现硬件-软件协作仿真。
 *
 * 功能：
 * RegFileSync.sv 模块每周期调用 set_cpu_reg/set_cpu_csr 同步寄存器值.
 * - 存储器读写（pmem_read, pmem_write）
 * - EBREAK 指令处理（ebreak_handler）
 * 
 * set_cpu_reg/set_cpu_csr是硬件函数. 每个周期将硬件寄存器信号写入C++全局数组`cpu_regs`和`cpu_csrs`.
 * get_cpu_regs是软件函数.  直接`return cpu_regs;`.
 ***************************************************************************************/

#ifndef __DPIC_H__
#define __DPIC_H__

#include <cstdint>
#include "config.h"

// 前向声明 Verilator 生成的类
class VMiniRV;

// ============ 存储器接口 ============

/**
 * pmem_read - 从存储器读取 32 位数据
 * @param raddr: 读地址（已按 4 字节对齐）
 * @return: 32 位数据
 */
extern "C" uint32_t pmem_read(uint32_t raddr);

/**
 * pmem_write - 向存储器写入数据
 * @param waddr: 写地址（已按 4 字节对齐）
 * @param wdata: 写数据（32 位）
 * @param wmask: 写掩码（按字节，低 4 位有效）
 */
extern "C" void pmem_write(int waddr, int wdata, char wmask);

/**
 * set_cpu_reg - 这是DPIC函数. 同步单个 GPR 寄存器值. 由硬件 RegFileSync每个周期调用
 * @param idx: 寄存器索引 (0-31)
 * @param value: 寄存器值
 *
 * 【架构说明】
 * - 由 RegFileSync.sv 每周期调用，维护 cpu_regs[] 数组
 * - 条件编译：仅在 ENABLE_SDB || ENABLE_DIFFTEST 时实际同步，否则编译为空桩
 * - 仿真器无关：标准 DPI-C 接口，支持 Verilator/VCS/Questa 等所有仿真器
 */
extern "C" void set_cpu_reg(int idx, int value);

/**
 * set_cpu_csr - 这是DPIC函数. 同步单个 CSR 寄存器值. 由硬件 RegFileSync每个周期调用
 * @param idx: CSR 索引 (0=mstatus, 1=mtvec, 2=mepc, 3=mcause, 4=mcycle, 5=mcycleh, 6=mvendorid, 7=marchid)
 * @param value: CSR 值
 */
extern "C" void set_cpu_csr(int idx, int value);

/**
 * get_cpu_regs - 获取 CPU 寄存器数组指针（SDB 和 DiffTest 共用）
 * @return: 指向 32 个通用寄存器数组的指针
 *
 * 【数据来源】
 * - 通过 set_cpu_reg() 从硬件同步而来（每周期更新）
 * - 不依赖特定仿真器的内部 API，实现了硬件/软件解耦
 */
#if ENABLE_SDB || ENABLE_DIFFTEST
uint32_t* get_cpu_regs();
#endif

/**
 * set_dut_ptr - 设置 DUT 指针，使 dpic 模块可以直接访问 Verilator 内部信号
 * @param dut: Verilator 生成的 VMiniRV 实例指针
 * 
 * 【调用时机】在 main() 中创建 DUT 实例后立即调用
 */
void set_dut_ptr(VMiniRV* dut);

// ============ TRAP 处理接口 ============

/**
 * ebreak_handler - 处理 EBREAK 指令
 * 在仿真中检测到 EBREAK 时调用，读取 a0 (x10) 寄存器作为退出码
 */
extern "C" void ebreak_handler();

// ============ 外设初始化接口 ============

/**
 * init_device - 初始化外设（串口、时钟等）
 * 
 * 【调用时机】在仿真开始前调用，记录系统启动时间
 */
void init_device();

#endif // __DPIC_H__
