/*
cpu的内存模拟. 
* 一个全局变量:
ram_words, 指向verilator里硬件ram寄存器.
* 提供操作这个全局变量的函数:
加载.bin客户程序到硬件RAM中
按字读写硬件RAM(在cpu运行前加载镜像, 运行时调试器看内存, difftest时用到)
*/
#include "memory/memory.h"

#include <cassert>
#include <cstdio>
#include <algorithm>

// .bin 按 RISC-V 小端序排列，直接读入 uint32_t 数组要求宿主也使用小端序。
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              "Direct RAM image loading requires a little-endian host");

// 编译时检查平台布局，修改地址配置后由 C++ 编译器直接报告错误。
static constexpr uint64_t address_space = uint64_t(1) << 32;
static constexpr uint64_t ram_end = uint64_t(NPC_RAM_BASE) + NPC_RAM_SIZE;
static_assert(NPC_RAM_SIZE > 0 && NPC_RAM_BASE < address_space &&
              NPC_RAM_SIZE <= address_space - NPC_RAM_BASE,
              "RAM must fit in the RV32 physical address space");
static_assert(NPC_RAM_BASE % 4 == 0 && NPC_RAM_SIZE % 4 == 0,
              "RAM base and size must be multiples of four bytes");
static_assert(NPC_UART_BASE % 4 == 0 && NPC_UART_BASE <= address_space - 8,
              "UART must be word aligned and fit in RV32");
static_assert(NPC_RTC_BASE % 4 == 0 && NPC_RTC_BASE <= address_space - 8,
              "RTC must be word aligned and fit in RV32");
static_assert(ram_end <= NPC_UART_BASE || uint64_t(NPC_UART_BASE) + 8 <= NPC_RAM_BASE,
              "RAM overlaps UART");
static_assert(ram_end <= NPC_RTC_BASE || uint64_t(NPC_RTC_BASE) + 8 <= NPC_RAM_BASE,
              "RAM overlaps RTC");
static_assert(uint64_t(NPC_UART_BASE) + 8 <= NPC_RTC_BASE ||
              uint64_t(NPC_RTC_BASE) + 8 <= NPC_UART_BASE,
              "UART overlaps RTC");

// cpu内存 RAM 在仿真器中对应的存储阵列，由 Verilator 持有。
// ram_words 只保存指针；CPU 的 AXI 写入和这里的调试读取访问同一份数据。
static uint32_t *ram_words = nullptr;

bool memory_bind_ram(uint32_t base, size_t words, uint32_t *storage) {
    if (base != NPC_RAM_BASE || words != NPC_RAM_SIZE / 4 ||
        !storage) {
        printf("[ERROR] Hardware RAM [0x%08x, +%zu bytes) does not match "
               "software RAM [0x%08lx, +%lu bytes)\n",
               base, words * 4, NPC_RAM_BASE, NPC_RAM_SIZE);
        return false;
    }
    ram_words = storage;
    return true;
}

void memory_unbind_ram() {
    ram_words = nullptr;
}

// 判断给定cpu物理地址区间是否合法
bool memory_contains(uint32_t addr, size_t size) {
    return addr >= NPC_RAM_BASE && size <= NPC_RAM_SIZE &&
           uint64_t(addr) - NPC_RAM_BASE <= NPC_RAM_SIZE - size;
}

// 把cpu看到的物理地址addr转换成RAM的字节偏移index. NPC_RAM_BASE来自npc.mk
static size_t addr_to_index(uint32_t addr) {
    return addr - NPC_RAM_BASE;
}

// 从硬件RAM中读一个word(32bit), 按小端序拼出来.
uint32_t memory_read_word(uint32_t addr) {
    assert(ram_words && (addr & 3) == 0 && memory_contains(addr, 4));
    const size_t index = addr_to_index(addr) / 4;
    return ram_words[index];
}

// 向硬件RAM写入一个word（用于仿真初始化、调试；CPU 运行时通过 AXI 写入）
void memory_write_word(uint32_t addr, uint32_t data, uint8_t mask) {
    assert(ram_words && (addr & 3) == 0 && memory_contains(addr, 4));
    const size_t index = addr_to_index(addr) / 4;
    for (unsigned i = 0; i < 4; ++i) {
        if (mask & (1u << i)) {
            const uint32_t bits = 0xffu << (8 * i);
            ram_words[index] = (ram_words[index] & ~bits) | (data & bits);
        }
    }
}

// CPU 初始化并绑定 RAM 后，将.bin客户程序直接读入硬件RAM。
bool memory_load_image(const char *filename, ProgramImage &image) {
    assert(ram_words);
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        printf("[ERROR] Cannot open file: %s\n", filename);
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    const long size = ftell(fp);
    if (size < 4 || uint64_t(size) > NPC_RAM_SIZE) {
        printf("[ERROR] Invalid image size: %ld bytes (RAM capacity: %lu)\n",
               size, NPC_RAM_SIZE);
        fclose(fp);
        return false;
    }
    rewind(fp);

    // 保留原来静态数组的初始零值约定；初始化不经过 CPU 总线，也不产生 MTrace。
    std::fill_n(ram_words, NPC_RAM_SIZE / 4, uint32_t(0));
    // 将.bin文件直接读入已经创建并绑定的 RAM 数组。
    const size_t loaded = fread(ram_words, 1, size, fp);
    fclose(fp);
    if (loaded != size_t(size)) {
        printf("[ERROR] Incomplete image read: %zu of %ld bytes\n", loaded, size);
        return false;
    }
    image = {uint32_t(NPC_RAM_BASE), loaded,
             reinterpret_cast<const uint8_t *>(ram_words)};
    printf("[INFO] RAM [0x%08lx, 0x%08lx), %lu bytes\n",
           NPC_RAM_BASE, NPC_RAM_BASE + NPC_RAM_SIZE, NPC_RAM_SIZE);
    printf("[INFO] Loaded %zu bytes from %s into hardware RAM at 0x%08x\n",
           image.size, filename, image.load_addr);
    return true;
}
