/***************************************************************************************
 * dpic.cpp - DPI-C 函数实现
 * 
 * 实现硬件-软件协作仿真的 DPI-C 接口，包括存储器操作、寄存器同步和 TRAP 处理
 ***************************************************************************************/

// 配置文件（必须在其他 trace 头文件之前 include）
#include "config.h"

#include "dpic.h"
#include "trace/itrace.h"
#include "trace/mtrace.h"
#include "trace/ftrace.h"
#include "trace/dtrace.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>       // 用于时钟功能

// Verilator 生成的头文件（用于直接访问内部信号）
#include "VAzazeRV.h"
#include "VAzazeRV___024root.h"

// ========================== 存储器定义 ==============================
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

// ================================ MMIO 外设地址定义 ===================================
// 串口地址（与 SoC 保持一致）
#define SERIAL_PORT 0x10000000UL

// 时钟地址. 随便写了一个值哈. pmem_read尝试读这个地址, 发现是RTC_ADDR, 就会调用get_time_us(), 调用linux本机的库函数实现获取时间.
#define RTC_ADDR    0xa0000048UL

// 系统启动时间（用于计算 uptime）
static uint64_t g_boot_time = 0;


/**
 * get_time_us() - 利用库函数获取当前系统时间（微秒）.
 * 
 * @return 当前时间戳，单位为微秒 (us)
 ***************************************************************************************/
static uint64_t get_time_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}


/** 
 * init_device() - 初始化外设
 * 
 * 在仿真开始时调用，记录启动时间
 */
void init_device() {
    g_boot_time = get_time_us();
}


// ============ DUT 指针（ebreak_handler 和 DiffTest 共用）============
// 由 main.cpp 调用 set_dut_ptr() 设置，用于直接访问 Verilator 内部信号
static VAzazeRV* g_dut = nullptr;

// 全局仿真周期计数（由 main.cpp 定义，此处声明为外部变量）
extern uint64_t g_cycle;

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

/***************************************************************************************
 * pmem_read - 从存储器或 MMIO 设备读取 32 位数据
 * 
 * @param raddr: 读取的物理地址（会自动对齐到 4 字节边界）
 * @return 读取到的 32 位数据；对于无效地址返回 0
 * 
 * 【MMIO 设备】
 * - RTC_ADDR (0xa0000048): 返回系统启动后经过的时间
 *   - 偏移 0: 低 32 位 (us)
 *   - 偏移 4: 高 32 位 (us)
 ***************************************************************************************/
extern "C" uint32_t pmem_read(uint32_t raddr) {
    uint32_t addr = raddr & ~0x3u;  // 对齐到 4 字节边界
    
    // -------- MMIO: 时钟 (RTC) --------
    // 返回系统启动后经过的微秒数
    if (addr == RTC_ADDR) {
        uint64_t uptime = get_time_us() - g_boot_time;
        uint32_t data = (uint32_t)(uptime & 0xFFFFFFFF);  // 低 32 位
        dtrace.log_read("RTC", addr, data, 4);
        return data;
    }
    if (addr == RTC_ADDR + 4) {
        uint64_t uptime = get_time_us() - g_boot_time;
        uint32_t data = (uint32_t)(uptime >> 32);         // 高 32 位
        dtrace.log_read("RTC", addr, data, 4);
        return data;
    }
    
    // -------- 普通内存访问 --------
    if (!addr_valid(addr)) {
        // 流水线初始化阶段或非 Load 指令时可能读取无效地址，静默返回 0
        return 0;
    }
    
    uint32_t idx = addr_to_index(addr);
    uint32_t data = *(uint32_t*)(mem + idx);
    
    mtrace.write(addr, data, 4, MTRACE_READ, g_current_pc, g_cycle);

    return data;
}

/***************************************************************************************
 * pmem_write - 向存储器或 MMIO 设备写入数据
 * 
 * @param waddr: 写入的物理地址（会自动对齐到 4 字节边界）
 * @param wdata: 要写入的 32 位数据
 * @param wmask: 字节写掩码，每个 bit 对应 wdata 中的一个字节
 *               例如 wmask=0x3 表示只写入最低 2 字节
 * 
 * 【MMIO 设备】
 * - SERIAL_PORT (0x10000000): 串口输出，写入最低字节到终端
 ***************************************************************************************/
extern "C" void pmem_write(int waddr, int wdata, char wmask) {
    uint32_t addr = (uint32_t)waddr & ~0x3u;  // 对齐到 4 字节边界
    uint32_t data = (uint32_t)wdata;
    uint8_t mask = (uint8_t)wmask & 0x0F;

    // -------- MMIO: 串口输出 --------
    // 将最低字节输出到终端
    if (addr == SERIAL_PORT) {
        if (mask & 1) {
            uint8_t ch = data & 0xFF;
            dtrace.log_write("SERIAL", addr, ch, 1);
            putchar(ch);
            fflush(stdout); // 确保字符立即输出
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

    dtrace.log_write("MEM", addr, data, len);

    mtrace.write(addr, data, len, MTRACE_WRITE, g_current_pc, g_cycle);

    // 按字节掩码写入
    for (int i = 0; i < 4; i++) {
        if (mask & (1 << i)) {
            mem[idx + i] = (data >> (i * 8)) & 0xFF;
        }
    }
}

/**
 * ebreak_handler - 处理 EBREAK 指令
 * 直接从 Verilator 内部信号读取 a0，不依赖 RegFileSync/cpu_regs[] 链路。
 */
extern "C" void ebreak_handler() {
    // 直接读取 Verilator 内部寄存器堆，与 DiffTest 开关无关
    uint32_t exit_code = g_dut ? g_dut->rootp->AzazeRV__DOT__gprfile__DOT__regs_10 : 0;
    
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
