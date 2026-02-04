/***************************************************************************************
 * cpu.h - CPU 执行控制接口
 * 
 * 提供 CPU 仿真的核心控制函数，包括初始化、单步执行、批量执行等
 ***************************************************************************************/

#ifndef __CPU_H__
#define __CPU_H__

#include <cstdint>

// ============ 存储器定义 ============
#define MEM_SIZE (128 * 1024 * 1024)  // 128MB
#define MEM_BASE 0x80000000UL

// 存储器数组（由 main.cpp 定义）
extern uint8_t mem[MEM_SIZE];

// ============ CPU 状态 ============
enum CPUState {
    CPU_RUNNING,
    CPU_STOPPED,
    CPU_QUIT
};

// ============ CPU 控制接口 ============

/**
 * cpu_init - 初始化 CPU 仿真环境
 * @argc, @argv: 命令行参数（传递给 Verilator）
 */
void cpu_init(int argc, char** argv);

/**
 * cpu_reset - 复位 CPU
 */
void cpu_reset();

/**
 * cpu_exec - 执行 n 条指令
 * @n: 要执行的指令数，-1 表示执行到结束
 */
void cpu_exec(uint64_t n);

/**
 * cpu_exit - 清理并退出 CPU 仿真
 */
void cpu_exit();



/**
 * cpu_set_max_cycles - 设置最大仿真周期数
 */
void cpu_set_max_cycles(uint64_t cycles);

/**
 * cpu_get_state - 获取 CPU 当前状态
 */
CPUState cpu_get_state();

// ============ 全局变量（供其他模块访问）============
extern uint64_t g_cycle;        // 当前周期数
extern uint32_t g_current_pc;   // 当前 PC

#endif // __CPU_H__
