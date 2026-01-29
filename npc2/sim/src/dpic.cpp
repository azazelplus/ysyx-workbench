/***************************************************************************************
 * dpic.cpp - DPI-C 函数实现
 * 
 * 实现硬件-软件协作仿真的 DPI-C 接口，包括存储器操作、寄存器同步和 TRAP 处理
 ***************************************************************************************/

// 配置文件（必须在其他 trace 头文件之前 include）
#include "config.h"

#include "dpic.h"
#include "itrace.h"
#include "mtrace.h"
#include "ftrace.h"
#include <cstdio>
#include <cstdlib>

// Verilator 生成的头文件（用于直接访问内部信号）
#include "VMiniRV.h"
#include "VMiniRV___024root.h"

// ============ 存储器定义 ============
// 存储器大小：128MB
#define MEM_SIZE (128 * 1024 * 1024)

// 存储器基地址 (RISC-V 典型的程序起始地址)
#define MEM_BASE 0x80000000UL

// 存储器数组（由 main.cpp 定义，此处声明为外部变量）
extern uint8_t mem[MEM_SIZE];

// 当前执行的 PC（由 main.cpp 更新，用于 mtrace）
extern uint32_t g_current_pc;

// 检查地址是否在有效范围内
static inline bool addr_valid(uint32_t addr) {
    return (addr >= MEM_BASE) && (addr < MEM_BASE + MEM_SIZE);
}

// 将物理地址转换为存储器数组下标
static inline uint32_t addr_to_index(uint32_t addr) {
    return addr - MEM_BASE;
}

// ============ CPU 寄存器访问 ============
// DUT 指针（由 main.cpp 设置，用于直接访问 Verilator 内部信号）
static VMiniRV* g_dut = nullptr;

// 寄存器缓冲区（由 get_cpu_regs 填充）
static uint32_t cpu_regs[32] = {0};

// 全局仿真周期计数（由 main.cpp 定义，此处声明为外部变量）
extern uint64_t g_cycle;

/**
 * set_dut_ptr - 设置 DUT 指针
 * 在 main() 创建 VMiniRV 实例后调用，使本模块可以直接访问 Verilator 内部信号
 */
void set_dut_ptr(VMiniRV* dut) {
    g_dut = dut;
}

/**
 * get_cpu_regs - 获取 CPU 寄存器数组指针 (供 DiffTest 使用)
 * 
 * 【实现方式】直接从 Verilator 内部信号读取寄存器值，实时反映硬件状态
 * 不再依赖 DPI-C 的 set_cpu_reg 同步，避免了时序延迟问题
 */
uint32_t* get_cpu_regs() {
    if (g_dut) {
        // 直接从 Verilator 内部信号读取寄存器值
        // 这些变量名由 Verilator 根据 Chisel 层级结构生成
        cpu_regs[0]  = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_0;
        cpu_regs[1]  = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_1;
        cpu_regs[2]  = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_2;
        cpu_regs[3]  = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_3;
        cpu_regs[4]  = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_4;
        cpu_regs[5]  = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_5;
        cpu_regs[6]  = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_6;
        cpu_regs[7]  = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_7;
        cpu_regs[8]  = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_8;
        cpu_regs[9]  = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_9;
        cpu_regs[10] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_10;
        cpu_regs[11] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_11;
        cpu_regs[12] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_12;
        cpu_regs[13] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_13;
        cpu_regs[14] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_14;
        cpu_regs[15] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_15;
        cpu_regs[16] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_16;
        cpu_regs[17] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_17;
        cpu_regs[18] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_18;
        cpu_regs[19] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_19;
        cpu_regs[20] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_20;
        cpu_regs[21] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_21;
        cpu_regs[22] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_22;
        cpu_regs[23] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_23;
        cpu_regs[24] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_24;
        cpu_regs[25] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_25;
        cpu_regs[26] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_26;
        cpu_regs[27] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_27;
        cpu_regs[28] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_28;
        cpu_regs[29] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_29;
        cpu_regs[30] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_30;
        cpu_regs[31] = g_dut->rootp->MiniRV__DOT__regfile__DOT__regs_31;
    }
    return cpu_regs;
}

// ============ DPI-C 函数实现 ============

/**
 * pmem_read - 从存储器读取 32 位数据
 */
extern "C" uint32_t pmem_read(uint32_t raddr) {
    uint32_t addr = raddr;
    
    if (!addr_valid(addr)) {
        // 流水线初始化阶段或非 Load 指令时可能读取无效地址，静默返回 0
        return 0;
    }
    
    uint32_t idx = addr_to_index(addr);
    uint32_t data = *(uint32_t*)(mem + idx);
    
    mtrace.write(addr, data, 4, MTRACE_READ, g_current_pc, g_cycle);

    return data;
}

/**
 * pmem_write - 向存储器写入数据
 */
extern "C" void pmem_write(int waddr, int wdata, char wmask) {
    uint32_t addr = (uint32_t)waddr;
    uint32_t data = (uint32_t)wdata;
    uint8_t mask = (uint8_t)wmask & 0x0F;

    // 串行端口输出
    if (addr == 0xa00003f8) {
        if (mask & 1) {
            putchar(data & 0xFF);
            fflush(stdout);
        }
        return;
    }

    // 检查有效地址
    if (!addr_valid(addr)) {
        printf("[WARN] pmem_write: invalid address 0x%08x (ignored)\n", addr);
        return;
    }
    
    uint32_t idx = addr_to_index(addr);
    
    // 计算写入长度 (根据 mask)
    uint8_t len = 0;
    for (int i = 0; i < 4; i++) {
        if (mask & (1 << i)) len++;
    }

    mtrace.write(addr, data, len, MTRACE_WRITE, g_current_pc, g_cycle);

    // 按字节掩码写入
    for (int i = 0; i < 4; i++) {
        if (mask & (1 << i)) {
            mem[idx + i] = (data >> (i * 8)) & 0xFF;
        }
    }
}

/**
 * set_cpu_reg - 同步单个寄存器值
 */
extern "C" void set_cpu_reg(int idx, int value) {
    if (idx >= 0 && idx < 32) {
        cpu_regs[idx] = (uint32_t)value;
    }
}

/**
 * ebreak_handler - 处理 EBREAK 指令
 */
extern "C" void ebreak_handler() {
    int exit_code = (int)cpu_regs[10];  // a0 = x10
    
    printf("\n[INFO] Simulation ended after %lu cycles.\n", g_cycle);

    if (exit_code == 0) {
        printf("[INFO] HIT GOOD TRAP (a0 = 0)\n");
    } else {
        printf("[ERROR] HIT BAD TRAP (a0 = %d)\n", exit_code);
        // BAD TRAP 时显示追踪缓冲区
#if ENABLE_ITRACE
        itrace.display_ringbuf();
#endif
#if ENABLE_MTRACE
        mtrace.display_ringbuf();
#endif
#if ENABLE_FTRACE
        ftrace.display_ringbuf();
#endif
    }
    exit(exit_code);
}
