/***************************************************************************************
 * main.cpp - 仿真主程序
 * 
 * 职责：
 * - 管理 Verilator 仿真循环
 * - 加载程序到存储器
 * - 协调指令追踪和 DPI-C 回调
 * 
 * 
 ***************************************************************************************/

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

// Verilator 头文件
#include "verilated.h"
#include "verilated_vcd_c.h"
#include "VMiniRV.h"

// 模块头文件
#include "dpic.h"
#include "disasm.h"
#include "itrace.h"
#include "mtrace.h"
#include "ftrace.h"

// ============ 用户配置区域 (Global Config) ============

// --- 指令追踪 (ITrace) ---
const bool CONF_ITRACE_ENABLE    = true;  // 是否开启指令追踪
const bool CONF_ITRACE_REALTIME  = false; // 是否实时打印 (false=仅出错时显示)

// --- 内存追踪 (MTrace) ---
const bool CONF_MTRACE_ENABLE    = true;  // 是否开启内存追踪
const bool CONF_MTRACE_REALTIME  = false; // 是否实时打印
const bool CONF_MTRACE_RANGE_EN  = false; // 是否开启地址过滤
const uint32_t CONF_MTRACE_START = 0x80000000;
const uint32_t CONF_MTRACE_END   = 0x80001000;

// --- 函数追踪 (FTrace) ---
const bool CONF_FTRACE_ENABLE    = true;  // 是否开启函数追踪
const bool CONF_FTRACE_REALTIME  = false; // 是否实时打印

// ==================================================



// ============ 仿真配置 ============
// 默认最大仿真周期数（可通过命令行参数覆盖）

// ============ 仿真配置 ============
// 默认最大仿真周期数（可通过命令行参数覆盖）
#define DEFAULT_MAX_CYCLES 5000000


// ============ 存储器定义 ============
// 存储器大小：128MB
#define MEM_SIZE (128 * 1024 * 1024)

// 存储器基地址 (RISC-V 典型的程序起始地址)
#define MEM_BASE 0x80000000UL

// 存储器数组（由 dpic.cpp 访问）
uint8_t mem[MEM_SIZE];

// 全局仿真周期计数（由 dpic.cpp 访问）
uint64_t g_cycle = 0;

// 当前执行的 PC（由 dpic.cpp 用于 mtrace）
uint32_t g_current_pc = 0;

// ============ 程序加载 ============
/**
 * 从二进制文件加载程序到存储器
 * @param filename: 二进制文件路径
 * @return: 成功返回加载的字节数，失败返回 -1
 */
static long load_program(const char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        printf("[ERROR] Cannot open file: %s\n", filename);
        return -1;
    }
    
    // 获取文件大小
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (size > MEM_SIZE) {
        printf("[ERROR] Program too large: %ld bytes (max %d)\n", size, MEM_SIZE);
        fclose(fp);
        return -1;
    }
    
    // 读取文件内容到存储器
    size_t read_size = fread(mem, 1, size, fp);
    fclose(fp);
    
    printf("[INFO] Loaded %ld bytes from %s\n", read_size, filename);
    return read_size;
}

// ============ 仿真主函数 ============
int main(int argc, char** argv) {
    // 1. 检查命令行参数
    if (argc < 2) {
        printf("Usage: %s <program.bin> [elf_file] [max_cycles]\n", argv[0]);
        printf("  <program.bin>: Binary program file to load\n");
        printf("  [elf_file]:    ELF file for ftrace (optional)\n");
        printf("  [max_cycles]:  Maximum simulation cycles (default: %d)\n", DEFAULT_MAX_CYCLES);
        return 1;
    }
    
    // 2. 加载程序
    long prog_size = load_program(argv[1]);
    if (prog_size < 0) {
        return 1;
    }
    
    // 3. 解析 ELF 文件和最大仿真周期数
    const char* elf_file = nullptr;
    int max_cycles = DEFAULT_MAX_CYCLES;
    
    // 检查 argv[2] 是 ELF 文件还是数字
    if (argc > 2) {
        // 如果以数字开头，认为是 max_cycles
        if (argv[2][0] >= '0' && argv[2][0] <= '9') {
            max_cycles = atoi(argv[2]);
        } else {
            elf_file = argv[2];
            if (argc > 3) {
                max_cycles = atoi(argv[3]);
            }
        }
    }
    
    // 4. 初始化 Verilator
    Verilated::commandArgs(argc, argv);

    // 应用全局配置
    itrace.enable(CONF_ITRACE_ENABLE);
    itrace.set_realtime(CONF_ITRACE_REALTIME);

    mtrace.enable(CONF_MTRACE_ENABLE);
    mtrace.set_realtime(CONF_MTRACE_REALTIME);
    mtrace.set_range_filter(CONF_MTRACE_RANGE_EN, CONF_MTRACE_START, CONF_MTRACE_END);

    ftrace.enable(CONF_FTRACE_ENABLE);
    ftrace.set_realtime(CONF_FTRACE_REALTIME);
    if (elf_file) {
        ftrace.init_elf(elf_file);
    } else {
        printf("[INFO] No ELF file provided, ftrace disabled\n");
        ftrace.enable(false);
    }

    // 创建 DUT 实例
    VMiniRV* dut = new VMiniRV;
    
    // 波形追踪
    Verilated::traceEverOn(true);
    VerilatedVcdC* tfp = new VerilatedVcdC;
    dut->trace(tfp, 99);
    tfp->open("wave.vcd");
    
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("[INFO] Waveform will be generated at: %s/wave.vcd\n", cwd);
    }
    
    // 5. 仿真开始提示
    printf("[INFO] Starting simulation...\n");
    printf("[INFO] Max cycles: %d\n", max_cycles);
    
    // 6. 复位
    dut->clock = 0;
    dut->reset = 1;
    for (int i = 0; i < 5; i++) {
        dut->clock = !dut->clock;
        dut->eval();
        tfp->dump(i);
    }
    dut->reset = 0;
    
    // 7. 主仿真循环
    uint32_t last_pc = 0;
    uint32_t last_inst = 0;
    
    while (g_cycle < max_cycles && !Verilated::gotFinish()) {
        // 时钟上升沿
        dut->clock = 1;
        dut->eval();
        tfp->dump(g_cycle * 2 + 10);
        
        // 更新当前 PC (供 mtrace 使用)
        g_current_pc = dut->io_debug_pc;
        
        // 指令追踪
        if (dut->io_debug_pc != last_pc) {
            // 函数追踪: 传入上一条指令和当前 PC（作为 next_pc）
            if (last_pc != 0) {
                ftrace.trace(last_pc, last_inst, dut->io_debug_pc, g_cycle);
            }
            
            // 记录到指令追踪缓冲区
            itrace.write(dut->io_debug_pc, dut->io_debug_inst, g_cycle);
            
            last_pc = dut->io_debug_pc;
            last_inst = dut->io_debug_inst;
        }
        
        // 时钟下降沿
        dut->clock = 0;
        dut->eval();
        tfp->dump(g_cycle * 2 + 11);
        
        g_cycle++;
    }
    
    // 8. 仿真循环结束
    printf("\n[INFO] Simulation ended after %lu cycles.\n", g_cycle);
    
    // 清理
    tfp->close();
    delete tfp;
    delete dut;
    
    // 检查是否超时
    if (g_cycle >= max_cycles) {
        printf("[ERROR] Simulation timed out!\n");
        // 超时时在 stdout 显示追踪缓冲区
        itrace.display_ringbuf();
        mtrace.display_ringbuf();
        ftrace.display_ringbuf();
        return 1;
    }

    return 0;
}
