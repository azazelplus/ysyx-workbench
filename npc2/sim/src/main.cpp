/***************************************************************************************
 * main.cpp - 仿真主程序
 * 
 * 职责：
 * - 管理 Verilator 仿真循环
 * - 加载程序到存储器
 * - 协调指令追踪和 DPI-C 回调
 * 
 * 注意：DPI-C 函数实现已独立到 dpic.cpp
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

// ============ 指令追踪配置 ============
// 定义 CONFIG_ITRACE 来启用指令追踪功能
#define CONFIG_ITRACE 1

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
        printf("Usage: %s <program.bin> [max_cycles]\n", argv[0]);
        printf("  <program.bin>: Binary program file to load\n");
        printf("  [max_cycles]:  Maximum simulation cycles (default: %d)\n", DEFAULT_MAX_CYCLES);
        return 1;
    }
    
    // 2. 加载程序
    long prog_size = load_program(argv[1]);
    if (prog_size < 0) {
        return 1;
    }
    
    // 3. 最大仿真周期数（从命令行参数读取）
    int max_cycles = (argc > 2) ? atoi(argv[2]) : DEFAULT_MAX_CYCLES;
    
    // 4. 初始化 Verilator
    Verilated::commandArgs(argc, argv);
    
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
    
    while (g_cycle < max_cycles && !Verilated::gotFinish()) {
        // 时钟上升沿
        dut->clock = 1;
        dut->eval();
        tfp->dump(g_cycle * 2 + 10);
        
        // 指令追踪
        if (dut->io_debug_pc != last_pc) {
#ifdef CONFIG_ITRACE
            // 记录到指令追踪缓冲区（打印模式由 itrace.h 中的 ITRACE_REALTIME 控制）
            itrace.write(dut->io_debug_pc, dut->io_debug_inst, g_cycle);
#endif
            last_pc = dut->io_debug_pc;
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
        // 超时时显示指令追踪缓冲区
#ifdef CONFIG_ITRACE
        itrace.display_ringbuf();
#endif
        return 1;
    }

    return 0;
}
