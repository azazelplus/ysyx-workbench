/***************************************************************************************
 * dpic.cpp - DPI-C 函数实现
 * 
 * 实现硬件-软件协作仿真的 DPI-C 接口，包括存储器操作、寄存器同步和 TRAP 处理
 ***************************************************************************************/

// 配置文件（必须在其他 trace 头文件之前 include）
#include "config.h"

#include "cpu/cpu.h"
#include "dpi/dpic.h"
#include "memory/memory.h"
#include "trace/itrace.h"
#include "trace/mtrace.h"
#include "trace/ftrace.h"
#include "trace/dtrace.h"
#include <cstdio>
#include <cstdlib>

// Verilator 生成的头文件（用于直接访问内部信号）
#include "VAzazeRV.h"
#include "VAzazeRV___024root.h"

// ============ DUT 指针（ebreak_handler 和 DiffTest 共用）============
// 由 main.cpp 调用 set_dut_ptr() 设置，用于直接访问 Verilator 内部信号
static VAzazeRV* g_dut = nullptr;

/** 
 * set_dut_ptr() - 设置 DUT 指针
 * 
 * @param dut: DUT 指针
 */
void set_dut_ptr(VAzazeRV* dut) {
    g_dut = dut;
}

// ============ 寄存器同步数组（通过 DPI-C 从硬件同步） ============
// cpu_regs[]: 由 RegFileSync 通过 set_cpu_reg() 每周期更新
// cpu_csrs[]: 由 RegFileSync 通过 set_cpu_csr() 每周期更新
//
// 【架构优势】
// - 仿真器无关：不依赖 Verilator 内部 API，可移植到 VCS/Questa/Cadence
// - 标准接口：使用 IEEE 1800 标准 DPI-C，硬件/软件解耦
// - 零开销：当 SDB 和 DiffTest 都关闭时，编译为空桩（编译器优化）
#if ENABLE_SDB || ENABLE_DIFFTEST
static uint32_t cpu_regs[32] = {0};  // GPR[0..31]
static uint32_t cpu_csrs[8]  = {0};  // CSR: mstatus, mtvec, mepc, mcause, mcycle, mcycleh, mvendorid, marchid
#endif

// ============ DPI-C 接口实现（由硬件调用） ============
/***************************************************************************************
 * set_cpu_reg - 同步单个 GPR 寄存器值（标准 DPI-C 接口）
 *
 * 由 RegFileSync.sv 每个时钟上升沿调用 32 次，同步所有 GPR 到 C++ 环境。
 *
 * @param idx: 寄存器索引 (0-31)
 * @param value: 寄存器值
 *
 * 【条件编译】
 * - ENABLE_SDB || ENABLE_DIFFTEST = 1: 真正维护 cpu_regs[] 数组
 * - 否则：编译为空桩，编译器会优化掉硬件调用（零开销）
 ***************************************************************************************/
extern "C" void set_cpu_reg(int idx, int value) {
#if ENABLE_SDB || ENABLE_DIFFTEST
    if (idx >= 0 && idx < 32) {
        cpu_regs[idx] = (uint32_t)value;
    }
#else
    // 空桩：当 SDB 和 DiffTest 都关闭时，此函数不做任何事
    // 编译器会将其优化为 nop，硬件调用不产生实际开销
    (void)idx;
    (void)value;
#endif
}

/**
 * set_cpu_csr - 同步单个 CSR 寄存器值（标准 DPI-C 接口）
 *
 * 由 RegFileSync.sv 每个时钟上升沿调用 8 次，同步所有 CSR 到 C++ 环境。
 *
 * @param idx: CSR 索引 (0=mstatus, 1=mtvec, 2=mepc, 3=mcause, 4=mcycle, 5=mcycleh, 6=mvendorid, 7=marchid)
 * @param value: CSR 值
 */
extern "C" void set_cpu_csr(int idx, int value) {
#if ENABLE_SDB || ENABLE_DIFFTEST
    if (idx >= 0 && idx < 8) {
        cpu_csrs[idx] = (uint32_t)value;
    }
#else
    (void)idx;
    (void)value;
#endif
}


// ============ 软件读取接口 ============
#if ENABLE_SDB || ENABLE_DIFFTEST
/***************************************************************************************
 * get_cpu_regs - 获取 CPU 寄存器数组指针（SDB 和 DiffTest 共用）
 *
 * @return: 指向 32 个通用寄存器数组的指针
 ***************************************************************************************/
uint32_t* get_cpu_regs() {
    return cpu_regs;
}
#endif // ENABLE_SDB || ENABLE_DIFFTEST

// ============ DPI-C 函数实现 ============

// RAM 的数据由硬件维护；这两个入口仅记录实际握手，不再承担存储器读写。
extern "C" void ram_trace_read(uint32_t addr, uint32_t data) {
    mtrace.write(addr, data, 4, MTRACE_READ, g_current_pc, g_cycle);
}

extern "C" void ram_trace_write(uint32_t addr, uint32_t data, uint8_t mask) {
    // 计算写入长度 (根据 mask)
    uint8_t len = 0;
    for (int i = 0; i < 4; i++) {
        if (mask & (1 << i)) len++;
    }

    dtrace.log_write("MEM", addr, data, len);

    mtrace.write(addr, data, len, MTRACE_WRITE, g_current_pc, g_cycle);
}

// 保留原 PMEM 后端供对照；当前顶层使用 RAM.scala，不再实例化 PMEM。

/***************************************************************************************
 * pmem_read - 从普通内存读取 32 位数据
 * 
 * @param raddr: 读取的物理地址（会自动对齐到 4 字节边界）
 * @return 读取到的 32 位数据；对于无效地址返回 0
 * 
 ***************************************************************************************/
extern "C" uint32_t pmem_read(uint32_t raddr) {
    uint32_t addr = raddr & ~0x3u;  // 对齐到 4 字节边界
    
    // -------- 普通内存访问 --------
    if (!memory_contains(addr, 4)) {
        // 流水线初始化阶段或非 Load 指令时可能读取无效地址，静默返回 0
        return 0;
    }
    
    uint32_t data = memory_read_word(addr);
    
    ram_trace_read(addr, data);

    return data;
}

/***************************************************************************************
 * pmem_write - 向普通内存写入数据
 * 
 * @param waddr: 写入的物理地址（会自动对齐到 4 字节边界）
 * @param wdata: 要写入的 32 位数据
 * @param wmask: 字节写掩码，每个 bit 对应 wdata 中的一个字节
 *               例如 wmask=0x3 表示只写入最低 2 字节
 ***************************************************************************************/
extern "C" void pmem_write(int waddr, int wdata, char wmask) {
    uint32_t addr = (uint32_t)waddr & ~0x3u;  // 对齐到 4 字节边界
    uint32_t data = (uint32_t)wdata;
    uint8_t mask = (uint8_t)wmask & 0x0F;

    // 检查有效地址
    if (!memory_contains(addr, 4)) {
        printf("[WARN] pmem_write: invalid address 0x%08x (ignored)\n", addr);
        return;
    }
    
    ram_trace_write(addr, data, mask);

    memory_write_word(addr, data, mask);
}

/**
 * ebreak_handler - 处理 EBREAK 指令. 
 * 直接从 Verilator 内部信号读取 a0当退出码, 打印结果, 直接杀进程.
 */
extern "C" void ebreak_handler() {
    uint32_t exit_code = g_dut ? g_dut->rootp->AzazeRV__DOT__gprfile__DOT__regs_10 : 0; // 直接读取 Verilator 内部寄存器堆读x10(a0)
    
    printf("\n[INFO] Simulation ended after %lu cycles.\n", g_cycle);   //打印周期数

    // 按 a0判断成败
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
    exit(exit_code);    //调用C标准库函数. 在仿真器里调用它,效果就是整个 Verilator 仿真进程直接结束, shell 里 $? 拿到 exit_code
}
