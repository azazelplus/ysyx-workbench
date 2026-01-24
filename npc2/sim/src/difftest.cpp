/***************************************************************************************
 * difftest.cpp - NPC2 DiffTest (差分测试) 实现
 * 
 * 本文件实现 npc2 作为 DUT (Device Under Test) 时的 DiffTest 框架。
 * 通过动态加载 nemu.so (REF)，在每条指令执行后比较两者状态。
 * 
 * 【数据流】
 * 
 *   npc2 (DUT)                          nemu.so (REF)
 *   ┌─────────────┐                     ┌──────────────────┐
 *   │ Verilator   │                     │ difftest_init()  │
 *   │ 硬件仿真     │ ──dlopen加载──►     │ difftest_memcpy()│
 *   │             │                     │ difftest_regcpy()│
 *   │ 执行1条指令  │                     │ difftest_exec()  │
 *   └──────┬──────┘                     └────────▲─────────┘
 *          │                                     │
 *          │ difftest_step()                     │
 *          │ 1. ref_difftest_exec(1)  ───────────┘
 *          │ 2. ref_difftest_regcpy(DIFFTEST_TO_DUT)
 *          │ 3. 比较 gpr[0..31] 和 pc
 *          ▼
 *   比较结果 → 一致: 继续 / 不一致: 报错终止
 * 
 ***************************************************************************************/

#include "config.h"

#if ENABLE_DIFFTEST

#include "difftest.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <cassert>

// ============ REF API 函数指针 ============
// 这些函数在 REF (nemu.so) 中实现，通过 dlsym 动态获取

// 初始化 REF
static void (*ref_difftest_init)(int port) = nullptr;

// DUT 与 REF 内存拷贝
static void (*ref_difftest_memcpy)(uint32_t addr, void *buf, size_t n, bool direction) = nullptr;

// DUT 与 REF 寄存器拷贝
static void (*ref_difftest_regcpy)(void *dut, bool direction) = nullptr;

// 让 REF 执行 n 条指令
static void (*ref_difftest_exec)(uint64_t n) = nullptr;

// 在 REF 中触发中断
static void (*ref_difftest_raise_intr)(uint64_t NO) = nullptr;

// ============ 状态变量 ============
// 是否跳过下一次 REF 执行（用于处理特殊指令）
static bool is_skip_ref = false;

// ============ RISC-V 寄存器名称 (用于错误输出) ============
static const char *reg_names[32] = {
    "x0/zero", "x1/ra",  "x2/sp",  "x3/gp",  "x4/tp",  "x5/t0",  "x6/t1",  "x7/t2",
    "x8/s0",   "x9/s1",  "x10/a0", "x11/a1", "x12/a2", "x13/a3", "x14/a4", "x15/a5",
    "x16/a6",  "x17/a7", "x18/s2", "x19/s3", "x20/s4", "x21/s5", "x22/s6", "x23/s7",
    "x24/s8",  "x25/s9", "x26/s10","x27/s11","x28/t3", "x29/t4", "x30/t5", "x31/t6"
};

/***************************************************************************************
 * init_difftest - 初始化 DiffTest 框架
 * 
 * 【执行步骤】
 * 1. dlopen() 加载 REF 动态库
 * 2. dlsym() 获取 5 个 API 函数指针
 * 3. ref_difftest_init() 初始化 REF 内部状态
 * 4. ref_difftest_memcpy() 将 DUT 程序镜像同步到 REF 内存
 * 5. ref_difftest_regcpy() 将 DUT 初始寄存器同步到 REF
 ***************************************************************************************/
void init_difftest(const char *ref_so_file, long img_size, uint8_t *mem) {
    printf("[DiffTest] Initializing with REF: %s\n", ref_so_file);
    
    // 步骤 1: 加载 REF 动态库
    void *handle = dlopen(ref_so_file, RTLD_LAZY);
    if (!handle) {
        printf("[DiffTest] ERROR: Failed to load REF library: %s\n", dlerror());
        printf("[DiffTest] Please ensure nemu is compiled with CONFIG_TARGET_SHARE=y\n");
        printf("[DiffTest] Run: cd nemu && make menuconfig  (select 'Shared object')\n");
        printf("[DiffTest]      cd nemu && make\n");
        exit(1);
    }
    
    // 步骤 2: 获取 API 函数指针
    // 使用 dlsym 从动态库中查找符号
    ref_difftest_init = (void (*)(int))dlsym(handle, "difftest_init");
    assert(ref_difftest_init && "Failed to find difftest_init in REF");
    
    ref_difftest_memcpy = (void (*)(uint32_t, void*, size_t, bool))dlsym(handle, "difftest_memcpy");
    assert(ref_difftest_memcpy && "Failed to find difftest_memcpy in REF");
    
    ref_difftest_regcpy = (void (*)(void*, bool))dlsym(handle, "difftest_regcpy");
    assert(ref_difftest_regcpy && "Failed to find difftest_regcpy in REF");
    
    ref_difftest_exec = (void (*)(uint64_t))dlsym(handle, "difftest_exec");
    assert(ref_difftest_exec && "Failed to find difftest_exec in REF");
    
    ref_difftest_raise_intr = (void (*)(uint64_t))dlsym(handle, "difftest_raise_intr");
    assert(ref_difftest_raise_intr && "Failed to find difftest_raise_intr in REF");
    
    // 步骤 3: 初始化 REF
    ref_difftest_init(0);
    
    // 步骤 4: 同步内存 (DUT → REF)
    // 将程序镜像从 DUT 内存拷贝到 REF 内存
    ref_difftest_memcpy(DIFFTEST_MEM_BASE, mem, img_size, DIFFTEST_TO_REF);
    
    // 步骤 5: 同步初始寄存器 (DUT → REF)
    // DUT 初始状态: gpr[0..31] = 0, pc = 0x80000000
    // 构造寄存器缓冲区: 32 个 gpr + 1 个 pc = 33 个 uint32_t
    uint32_t init_regs[33];
    memset(init_regs, 0, sizeof(init_regs));
    init_regs[32] = DIFFTEST_MEM_BASE;  // pc = 0x80000000
    ref_difftest_regcpy(init_regs, DIFFTEST_TO_REF);
    
    printf("[DiffTest] Differential testing: \033[1;32mON\033[0m\n");
    printf("[DiffTest] REF loaded successfully, img_size = %ld bytes\n", img_size);
}

/***************************************************************************************
 * difftest_step - 执行一步差分测试
 * 
 * 【执行流程】
 * 1. 检查是否需要跳过 REF (处理特殊指令)
 * 2. 让 REF 执行 1 条指令
 * 3. 从 REF 读取寄存器状态
 * 4. 比较 DUT 和 REF 的 32 个 GPR 及 PC
 * 5. 不一致时打印详细错误并返回 false
 * 
 * 【参数说明】
 * @param pc:   DUT 当前指令的 PC（执行后的新 PC 值）
 * @param regs: DUT 当前的 32 个通用寄存器值
 * 
 * 【返回值】
 * true  = 比较通过，DUT 和 REF 状态一致
 * false = 检测到差异，DUT 实现可能有 bug
 ***************************************************************************************/
bool difftest_step(uint32_t pc, uint32_t *regs) {
    // 情况 1: 跳过 REF 执行（处理特殊指令如 ebreak）
    if (is_skip_ref) {
        // 将 DUT 状态直接同步到 REF，不执行 REF
        uint32_t dut_regs[33];
        memcpy(dut_regs, regs, 32 * sizeof(uint32_t));
        dut_regs[32] = pc;
        ref_difftest_regcpy(dut_regs, DIFFTEST_TO_REF);
        is_skip_ref = false;
        return true;
    }
    
    // 情况 2: 正常差分测试
    // 让 REF 执行 1 条指令
    ref_difftest_exec(1);
    
    // 从 REF 读取执行后的寄存器状态
    uint32_t ref_regs[33];  // 32 GPRs + PC
    ref_difftest_regcpy(ref_regs, DIFFTEST_TO_DUT);
    
    // 比较 32 个通用寄存器
    bool match = true;
    for (int i = 0; i < 32; i++) {
        if (regs[i] != ref_regs[i]) {
            printf("\n[DiffTest] \033[1;31mMISMATCH\033[0m at PC = 0x%08x\n", pc);
            printf("[DiffTest] Register %s:\n", reg_names[i]);
            printf("           DUT = 0x%08x\n", regs[i]);
            printf("           REF = 0x%08x\n", ref_regs[i]);
            match = false;
        }
    }
    
    // 比较 PC
    if (pc != ref_regs[32]) {
        printf("\n[DiffTest] \033[1;31mMISMATCH\033[0m at PC = 0x%08x\n", pc);
        printf("[DiffTest] PC:\n");
        printf("           DUT = 0x%08x\n", pc);
        printf("           REF = 0x%08x\n", ref_regs[32]);
        match = false;
    }
    
    if (!match) {
        // 打印完整的 DUT 寄存器状态供调试
        printf("\n[DiffTest] DUT Register Dump:\n");
        for (int i = 0; i < 32; i += 4) {
            printf("  %s=0x%08x  %s=0x%08x  %s=0x%08x  %s=0x%08x\n",
                   reg_names[i], regs[i],
                   reg_names[i+1], regs[i+1],
                   reg_names[i+2], regs[i+2],
                   reg_names[i+3], regs[i+3]);
        }
        printf("  pc=0x%08x\n", pc);
        
        printf("\n[DiffTest] REF Register Dump:\n");
        for (int i = 0; i < 32; i += 4) {
            printf("  %s=0x%08x  %s=0x%08x  %s=0x%08x  %s=0x%08x\n",
                   reg_names[i], ref_regs[i],
                   reg_names[i+1], ref_regs[i+1],
                   reg_names[i+2], ref_regs[i+2],
                   reg_names[i+3], ref_regs[i+3]);
        }
        printf("  pc=0x%08x\n", ref_regs[32]);
    }
    
    return match;
}

/***************************************************************************************
 * difftest_skip_ref - 标记跳过下一次 REF 执行
 * 
 * 用于处理特殊指令，这些指令在 DUT 和 REF 中可能有不同行为：
 * - ebreak: npc2 用于终止仿真，nemu 用于进入调试模式
 * - 其他自定义指令
 * 
 * 调用此函数后，下一次 difftest_step() 会：
 * 1. 跳过 ref_difftest_exec()
 * 2. 将 DUT 状态直接同步到 REF
 * 3. 重置 skip 标志
 ***************************************************************************************/
void difftest_skip_ref() {
    is_skip_ref = true;
}

#else
// ENABLE_DIFFTEST = false 时的空实现（编译优化）
void init_difftest(const char *ref_so_file, long img_size, uint8_t *mem) {}
bool difftest_step(uint32_t pc, uint32_t *regs) { return true; }
void difftest_skip_ref() {}
#endif /* ENABLE_DIFFTEST */
