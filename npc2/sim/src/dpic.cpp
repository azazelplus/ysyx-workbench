/***************************************************************************************
 * dpic.cpp - DPI-C 函数实现
 * 
 * 实现硬件-软件协作仿真的 DPI-C 接口，包括存储器操作、寄存器同步和 TRAP 处理
 ***************************************************************************************/

#include "dpic.h"
#include "itrace.h"
#include "mtrace.h"
#include "ftrace.h"
#include <cstdio>
#include <cstdlib>

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

// ============ CPU 寄存器镜像 ============
// 由硬件端 RegFileSync 模块每周期同步
static uint32_t cpu_regs[32] = {0};

// 全局仿真周期计数（由 main.cpp 定义，此处声明为外部变量）
extern uint64_t g_cycle;

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
        itrace.display_ringbuf();
        mtrace.display_ringbuf();
        ftrace.display_ringbuf();
    }
    exit(exit_code);
}
