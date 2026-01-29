/***************************************************************************************
 * dpic.h - DPI-C 函数接口声明
 * 
 * DPI-C (Direct Programming Interface - C) 函数的声明，这些函数由 SystemVerilog 
 * 代码通过 DPI-C 调用，实现硬件-软件协作仿真。
 * 
 * 功能：
 * - 存储器读写（pmem_read, pmem_write）
 * - CPU 寄存器同步（set_cpu_reg）
 * - EBREAK 指令处理（ebreak_handler）
 ***************************************************************************************/

#ifndef __DPIC_H__
#define __DPIC_H__

#include <cstdint>

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

// ============ CPU 寄存器接口 ============

/**
 * set_cpu_reg - 同步单个寄存器值 (由硬件 DPI-C 调用)
 * @param idx: 寄存器索引 (0-31)
 * @param value: 寄存器值
 */
extern "C" void set_cpu_reg(int idx, int value);

/**
 * get_cpu_regs - 获取 CPU 寄存器数组指针 (供 DiffTest 使用)
 * @return: 指向 32 个通用寄存器数组的指针
 * 
 * 【实现方式】直接从 Verilator 内部信号读取寄存器值，实时反映硬件状态
 */
uint32_t* get_cpu_regs();

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

#endif // __DPIC_H__
