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

// 配置文件（必须在其他 trace 头文件之前 include）
#include "config.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <getopt.h>

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
    // 默认配置
    const char *img_file = NULL;
    const char *elf_file = NULL;
    int max_cycles = DEFAULT_MAX_CYCLES;

    // 解析命令行参数 (getopt_long)
    static struct option long_options[] = {
        {"help",    no_argument,       0, 'h'},
        {"elf",     required_argument, 0, 'e'},
        {"cycles",  required_argument, 0, 'c'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "he:c:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: %s [options] <program.bin>\n", argv[0]);
                printf("Options:\n");
                printf("  -e, --elf <file>     ELF file for ftrace\n");
                printf("  -c, --cycles <num>   Maximum simulation cycles (default: %d)\n", DEFAULT_MAX_CYCLES);
                printf("  -h, --help           Show this help message\n");
                return 0;
            case 'e':
                elf_file = optarg;
                break;
            case 'c':
                max_cycles = atoi(optarg);
                break;
            default:
                printf("Unknown option: %c\n", opt);
                return 1;
        }
    }

    // 处理位置参数 (image file)
    if (optind < argc) {
        img_file = argv[optind];
    } else {
        printf("[ERROR] No image file specified!\n");
        printf("Usage: %s [options] <program.bin>\n", argv[0]);
        return 1;
    }

    // 2. 加载程序
    long prog_size = load_program(img_file);
    if (prog_size < 0) {
        return 1;
    }
    
    // 4. 初始化 Verilator
    Verilated::commandArgs(argc, argv);

    // 应用全局配置 - trace功能
#if ENABLE_ITRACE
    itrace.set_realtime(REALTIME_ITRACE);
#endif

#if ENABLE_MTRACE
    mtrace.set_realtime(REALTIME_MTRACE);
    mtrace.set_range_filter(MTRACE_RANGE_EN, MTRACE_START, MTRACE_END);
#endif

#if ENABLE_FTRACE
    ftrace.set_realtime(REALTIME_FTRACE);
    if (elf_file) {
        ftrace.init_elf(elf_file);
    } else {
        printf("[INFO] No ELF file provided, ftrace disabled\n");
    }
#endif

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
#if ENABLE_FTRACE
            // 函数追踪: 传入上一条指令和当前 PC（作为 next_pc）
            if (last_pc != 0) {
                ftrace.trace(last_pc, last_inst, dut->io_debug_pc, g_cycle);
            }
#endif
            
#if ENABLE_ITRACE
            // 记录到指令追踪缓冲区
            itrace.write(dut->io_debug_pc, dut->io_debug_inst, g_cycle);
#endif
            
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
        // 超时时显示追踪缓冲区
#if ENABLE_ITRACE
        itrace.display_ringbuf();
#endif
#if ENABLE_MTRACE
        mtrace.display_ringbuf();
#endif
#if ENABLE_FTRACE
        ftrace.display_ringbuf();
#endif
        return 1;
    }

    return 0;
}
