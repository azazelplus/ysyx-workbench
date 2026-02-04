/***************************************************************************************
 * cpu.cpp - CPU 执行控制实现
 * 
 * 管理 Verilator 仿真循环，提供 CPU 执行控制
 ***************************************************************************************/

#include "config.h"
#include "cpu.h"
#include "dpic.h"
#include "utils/disasm.h"
#include "trace/itrace.h"
#include "trace/mtrace.h"
#include "trace/ftrace.h"
#include "trace/dtrace.h"
#include "difftest.h"

#include "verilated.h"
#include "verilated_vcd_c.h"
#include "VMiniRV.h"

#include <cstdio>
#include <cstdlib>
#include <unistd.h>

// ============ 全局状态 ============
uint64_t g_cycle = 0;   //当前周期数
uint32_t g_current_pc = 0;  //当前周期pc值

static VMiniRV* dut = nullptr;
static VerilatedVcdC* tfp = nullptr;
static uint64_t max_cycles = DEFAULT_MAX_CYCLES;
static uint32_t last_pc = 0;
static uint32_t last_inst = 0;
static CPUState cpu_state = CPU_STOPPED;

// ============ 接口实现 ============

void cpu_set_max_cycles(uint64_t cycles) {
    max_cycles = cycles;
}

CPUState cpu_get_state() {
    return cpu_state;
}

void cpu_init(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    
    // 创建 DUT 实例
    dut = new VMiniRV;
    set_dut_ptr(dut);
    
    // 初始化外设
    init_device();
    
    // 初始化反汇编器
    init_disasm("riscv32");
    
    // 波形追踪
    Verilated::traceEverOn(true);
    tfp = new VerilatedVcdC;
    dut->trace(tfp, 99);
    tfp->open("wave.vcd");
    
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("[INFO] Waveform will be generated at: %s/wave.vcd\n", cwd);
    }
    
    printf("[INFO] Starting simulation...\n");
    printf("[INFO] Max cycles: %lu\n", max_cycles);
    
    // 复位
    cpu_reset();
    
    cpu_state = CPU_RUNNING;
}

void cpu_reset() {
    dut->clock = 0;
    dut->reset = 1;
    for (int i = 0; i < 5; i++) {
        dut->clock = !dut->clock;
        dut->eval();
        tfp->dump(i);
    }
    dut->reset = 0;
    
    last_pc = 0;
    last_inst = 0;
}

/**
 * exec_once - 执行一条指令
 * @return: 是否成功执行（未达到终止条件）
 */
static bool exec_once() {
    if (g_cycle >= max_cycles || Verilated::gotFinish()) {
        cpu_state = CPU_STOPPED;
        return false;
    }
    
    // 时钟上升沿
    dut->clock = 1;
    dut->eval();
    tfp->dump(g_cycle * 2 + 10);
    
    // 更新当前 PC
    g_current_pc = dut->io_debug_pc;
    
    // 时钟下降沿
    dut->clock = 0;
    dut->eval();
    tfp->dump(g_cycle * 2 + 11);
    
    // 检测指令完成（PC 变化）
    if (dut->io_debug_pc != last_pc) {
#if ENABLE_FTRACE
        if (last_pc != 0) {
            ftrace.trace(last_pc, last_inst, dut->io_debug_pc, g_cycle);
        }
#endif
        
#if ENABLE_ITRACE
        itrace.write(dut->io_debug_pc, dut->io_debug_inst, g_cycle);
#endif

#if ENABLE_DIFFTEST
        if (last_pc != 0) {
            if (!difftest_step(dut->io_debug_pc, get_cpu_regs())) {
                printf("\n[DiffTest] ERROR at cycle %lu, PC=0x%08x\n", g_cycle, last_pc);
#if ENABLE_ITRACE
                itrace.display_ringbuf();
#endif
#if ENABLE_MTRACE
                mtrace.display_ringbuf();
#endif
#if ENABLE_FTRACE
                ftrace.display_ringbuf();
#endif
                cpu_state = CPU_STOPPED;
                return false;
            }
        }
#endif
        
        last_pc = dut->io_debug_pc;
        last_inst = dut->io_debug_inst;
    }
    
    g_cycle++;
    return true;
}

void cpu_exec(uint64_t n) {
    if (cpu_state != CPU_RUNNING) {
        printf("[WARN] CPU is not running\n");
        return;
    }
    
    for (uint64_t i = 0; i < n && cpu_state == CPU_RUNNING; i++) {
        // 执行直到一条指令完成
        uint32_t pc_before = last_pc;
        while (last_pc == pc_before && cpu_state == CPU_RUNNING) {
            if (!exec_once()) break;
        }
    }
}

void cpu_exit() {
    printf("\n[INFO] Simulation ended after %lu cycles.\n", g_cycle);
    
    if (tfp) {
        tfp->close();
        delete tfp;
        tfp = nullptr;
    }
    
    if (dut) {
        delete dut;
        dut = nullptr;
    }
    
    if (g_cycle >= max_cycles) {
        printf("[ERROR] Simulation timed out!\n");
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
    
    cpu_state = CPU_QUIT;
}
