// MiniRV 仿真环境 - C++ 代码
// 实现 DPI-C 函数和存储器模拟.
// 使用方式: 在./sim/sim.sh脚本中, 调用verilator编译的命令里, 指定这个main.cpp参与编译链接.
// main.cpp提供了DPIC.scala中声明的DPI-C函数实现. DPIC.scala编译后, 产生sv代码:
    //PMEM_READ.sv, PMEM_WRITE.sv, EBREAK_HANDLER.sv  对应三个module.
    // 拿第一个模块举例. Verilator的行为:
        // 在编译 PMEM_READ.sv 时会识别到: 这个模块需要一个外部符号 `pmem_read`. 
        // 在构建 Verilator 可执行文件时, 你需要显式指定这个main.cpp给verilator一起编译和链接: `verilator --cc --exe --build ... ../sim/main.cpp -o VMiniRV`
        //`../sim/main.cpp -o VMiniRV`的意思就是: 把 main.cpp 编译进最终的 VMiniRV 可执行文件里.


#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

// Verilator 头文件
#include "verilated.h"
#include "verilated_vcd_c.h"
#include "VMiniRV.h"
#include "disasm.h"
#include <unistd.h>

// ============ 存储器定义 ============

// 存储器大小：128MB
#define MEM_SIZE (128 * 1024 * 1024)

// 存储器基地址 (RISC-V 典型的程序起始地址) UL表示编译器强制解释字面量为unsigned long.  wsl中UL是64bit. 其实可以用unsigned int(U)?
#define MEM_BASE 0x80000000UL

// 存储器数组
static uint8_t mem[MEM_SIZE];

// 检查地址是否在有效范围内
static inline bool addr_valid(uint32_t addr) {
    return (addr >= MEM_BASE) && (addr < MEM_BASE + MEM_SIZE);
}

// 将物理地址转换为存储器数组下标
static inline uint32_t addr_to_index(uint32_t addr) {
    return addr - MEM_BASE;
}


// ============ DPI-C 函数实现 ============
//extern "C" {...} 这些函数用C的方式导出. 不要用 C++ 的名字修饰规则（name mangling）.
// name mangling: 就是编译器为了支持函数重载等特性, 对函数偷偷改名.  你写一个int pmem_read(int); 编译器实际上会把它改成_Z10pmem_readi之类的. 
// C++等支持重载的高级语言有这个特性. C不会.
//原因: 此处我们需要让 SystemVerilog 代码能够调用这些函数. SystemVerilog 只能调用 C 风格的函数名(没有名字修饰).
extern "C" {

/**
 * pmem_read - 从存储器读取 32 位数据
 * @param raddr: 读地址（已按 4 字节对齐）
 * @return: 32 位数据
 */
//
uint32_t pmem_read(uint32_t raddr) {
    uint32_t addr = raddr;
    
    if (!addr_valid(addr)) {
        // 流水线初始化阶段或非 Load 指令时可能读取无效地址，静默返回 0
        // printf("[ERROR] pmem_read: invalid address 0x%08x\n", addr);
        return 0;
    }
    
    uint32_t idx = addr_to_index(addr);
    uint32_t data = *(uint32_t*)(mem + idx);
    
    // 调试输出（可选）
    // printf("[DEBUG] pmem_read: addr=0x%08x, data=0x%08x\n", addr, data);
    
    return data;
}

/**
 * pmem_write - 向存储器写入数据
 * @param waddr: 写地址（已按 4 字节对齐）
 * @param wdata: 写数据（32 位）
 * @param wmask: 写掩码（按字节，低 4 位有效）
 */
void pmem_write(int waddr, int wdata, char wmask) {
    uint32_t addr = (uint32_t)waddr;
    uint32_t data = (uint32_t)wdata;
    uint8_t mask = (uint8_t)wmask & 0x0F;

    if (addr == 0xa00003f8) {
        // Serial port output
        // Assume byte write (mask=1), but just take LSB for simplicity
        if (mask & 1) {
            putchar(data & 0xFF);
            fflush(stdout); // Ensure output is seen immediately
        }
        
        return;
    }

    if (addr == 0xa0000000) {
        // TRAP handling
        // printf("\n[INFO] HALT detected with code %d\n", data);
        if (data == 0) {
            printf("\n[INFO] HIT GOOD TRAP\n");
        } else {
            printf("\n[ERROR] HIT BAD TRAP\n");
        }
        exit(data);
    }
    
    if (!addr_valid(addr)) {
        // 无效写地址可能是程序错误，保留打印但可选注释
        printf("[WARN] pmem_write: invalid address 0x%08x (ignored)\n", addr);
        return;
    }
    
    uint32_t idx = addr_to_index(addr);
    
    // 按字节掩码写入
    for (int i = 0; i < 4; i++) {
        if (mask & (1 << i)) {
            mem[idx + i] = (data >> (i * 8)) & 0xFF;
        }
    }
    
    // 调试输出（可选）
    // printf("[DEBUG] pmem_write: addr=0x%08x, data=0x%08x, mask=0x%x\n", addr, data, mask);
}

/**
 * ebreak_handler - 处理 EBREAK 指令
 * 在仿真中检测到 EBREAK 时调用，用于终止仿真. 其实就是塞一个exit()函数到main里.
 */
void ebreak_handler() {
    printf("\n[INFO] EBREAK detected, simulation finished.\n");
    exit(0);
}

} // extern "C"

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
    // 检查命令行参数
    if (argc < 2) {
        printf("Usage: %s <program.bin> [max_cycles]\n", argv[0]);
        printf("  <program.bin>: Binary program file to load\n");
        printf("  [max_cycles]:  Maximum simulation cycles (default: 10000)\n");
        return 1;
    }
    
    // 加载程序
    long prog_size = load_program(argv[1]);
    if (prog_size < 0) {
        return 1;
    }
    
    // 最大仿真周期数
    int max_cycles = (argc > 2) ? atoi(argv[2]) : 10000;
    
    // 初始化 Verilator
    Verilated::commandArgs(argc, argv);
    
    // 创建 DUT 实例
    VMiniRV* dut = new VMiniRV;
    
    // 波形追踪（可选）
    Verilated::traceEverOn(true);
    VerilatedVcdC* tfp = new VerilatedVcdC;
    dut->trace(tfp, 99);
    tfp->open("wave.vcd");
    
    printf("[INFO] Starting simulation...\n");
    printf("[INFO] Max cycles: %d\n", max_cycles);

    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("[INFO] Waveform will be generated at: %s/wave.vcd\n", cwd);
    }
    
    // 复位
    dut->clock = 0;
    dut->reset = 1;
    for (int i = 0; i < 5; i++) {
        dut->clock = !dut->clock;
        dut->eval();
        tfp->dump(i);
    }
    dut->reset = 0;
    
    // 主仿真循环
    uint64_t cycle = 0;
    uint32_t last_pc = 0;
    
    while (cycle < max_cycles && !Verilated::gotFinish()) {
        // 时钟上升沿
        dut->clock = 1;
        dut->eval();
        tfp->dump(cycle * 2 + 10);
        
        // 打印当前 PC（可选）
        if (dut->io_debug_pc != last_pc) {
            std::string asm_str = disassemble(dut->io_debug_inst);
            printf("[CYCLE %5lu] PC=0x%08x, INST=0x%08x %s\n", 
                   cycle, dut->io_debug_pc, dut->io_debug_inst, asm_str.c_str());
            last_pc = dut->io_debug_pc;
        }
        
        // 时钟下降沿
        dut->clock = 0;
        dut->eval();
        tfp->dump(cycle * 2 + 11);
        
        cycle++;
    }
    
    printf("\n[INFO] Simulation ended after %lu cycles.\n", cycle);
    
    // 清理
    tfp->close();
    delete tfp;
    delete dut;
    
    // 如果正常运行通过, 应该在TRAP处理中调用exit(0)退出了.
    // 能运行到这里说明是超时(达到了max_cycles)或者Verilated::gotFinish(),
    // 在 cpu-tests 语境下, 超时应视为 FAIL.
    if (cycle >= max_cycles) {
        printf("[ERROR] Simulation timed out!\n");
        return 1;
    }

    return 0;
}
