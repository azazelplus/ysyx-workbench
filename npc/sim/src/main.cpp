/***************************************************************************************
 * main.cpp - 仿真主程序入口
 * 
 * 职责：
 * - 解析命令行参数
 * - 加载程序到存储器
 * - 初始化各模块配置
 * - 启动 SDB 或直接运行
 ***************************************************************************************/
#include "config.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <getopt.h>

#include "cpu.h"
#include "dpic.h"
#include "sdb.h"
#include "trace/ftrace.h"
#include "trace/mtrace.h"
#include "trace/itrace.h"
#include "difftest.h"
#include "memory.h"

// ===================== sim主函数 =======================
int main(int argc, char** argv) {
    // 默认配置
    const char *img_file = NULL;
    const char *elf_file = NULL;
    uint64_t max_cycles = DEFAULT_MAX_CYCLES;

    // 解析命令行参数
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
                max_cycles = atoll(optarg);
                break;
            default:
                printf("Unknown option: %c\n", opt);
                return 1;
        }
    }

    // 获取命令行参数提供的镜像客户程序, 如果有的话
    if (optind < argc) {
        img_file = argv[optind];
    } else {
        printf("[ERROR] No image file specified!\n");
        printf("Usage: %s [options] <program.bin>\n", argv[0]);
        return 1;
    }

    // 加载程序
    ProgramImage image{};
    if (!memory_load_image(img_file, image)) {
        return 1;
    }

    // 应用 trace 配置
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

    // 设置最大周期数并初始化 CPU
    cpu_set_max_cycles(max_cycles);
    cpu_init(argc, argv);

    // 当前镜像的入口位于首字节；从硬件读取复位地址，检查两侧是否匹配。
    const uint32_t reset_pc = cpu_get_reset_pc();
    printf("[INFO] Hardware reset PC: 0x%08x\n", reset_pc);
    if (image.load_addr != reset_pc) {
        printf("[ERROR] Image entry 0x%08x does not match hardware reset PC 0x%08x\n",
               image.load_addr, reset_pc);
        cpu_exit();
        return 1;
    }

#if ENABLE_DIFFTEST
    init_difftest(DIFFTEST_REF_PATH, image, reset_pc);
#endif

    // 根据配置决定运行模式
#if ENABLE_SDB
    sdb_mainloop();
#else
    cpu_exec(-1);  // 批处理模式：直接运行到结束
#endif

    // 检查是否超时退出（超时视为测试失败）
    bool timed_out = (g_cycle >= max_cycles);

    // 清理并退出
    cpu_exit();

    if (timed_out) {
        return 1;  // 超时返回非零，表示测试失败
    }
    return 0;
}
