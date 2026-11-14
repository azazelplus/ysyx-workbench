#include "memory/ram.h"
#include "memory/memory.h"
#include "VAzazeRV.h"
#include "VAzazeRV___024root.h"

// 将verilator里硬件ram的地址交给C++, 让pmem_read和pmem_write函数读写.
bool ram_bind(VAzazeRV *dut) {
    // Mem(depth, Vec(4, UInt(8.W))) 被 firtool 生成为带字节掩码的 32 位存储阵列。
    // 直接取得生成模型中的数组地址；这里的指针就是硬件 RAM 的实际存储。
    auto &ram = dut->rootp->AzazeRV__DOT__ram__DOT__mem__DOT__mem_ext__DOT__Memory;
    return memory_bind_ram(dut->io_debug_ram_base,
                           ram.size(), &ram[0]);
}
