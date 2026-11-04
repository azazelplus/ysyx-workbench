#include "memory.h"

#include <cassert>
#include <cstdio>

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

// 当前存储后端：DPI 仿真使用的宿主数组。数组的所有权收在内存模块内。
static uint8_t mem[NPC_RAM_SIZE];

bool memory_contains(uint32_t addr, size_t size) {
    return addr >= NPC_RAM_BASE && size <= NPC_RAM_SIZE &&
           uint64_t(addr) - NPC_RAM_BASE <= NPC_RAM_SIZE - size;
}

static size_t addr_to_index(uint32_t addr) {
    return addr - NPC_RAM_BASE;
}

uint32_t memory_read_word(uint32_t addr) {
    assert((addr & 3) == 0 && memory_contains(addr, 4));
    const size_t index = addr_to_index(addr);
    uint32_t data = 0;
    for (unsigned i = 0; i < 4; ++i) {
        data |= uint32_t(mem[index + i]) << (8 * i);
    }
    return data;
}

void memory_write_word(uint32_t addr, uint32_t data, uint8_t mask) {
    assert((addr & 3) == 0 && memory_contains(addr, 4));
    const size_t index = addr_to_index(addr);
    for (unsigned i = 0; i < 4; ++i) {
        if (mask & (1u << i)) {
            mem[index + i] = (data >> (8 * i)) & 0xff;
        }
    }
}

bool memory_load_image(const char *filename, ProgramImage &image) {
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
    const size_t loaded = fread(mem, 1, size, fp);
    fclose(fp);
    if (loaded != size_t(size)) {
        printf("[ERROR] Incomplete image read: %zu of %ld bytes\n", loaded, size);
        return false;
    }
    image = {uint32_t(NPC_RAM_BASE), loaded, mem};
    printf("[INFO] RAM [0x%08lx, 0x%08lx), %lu bytes\n",
           NPC_RAM_BASE, NPC_RAM_BASE + NPC_RAM_SIZE, NPC_RAM_SIZE);
    printf("[INFO] Loaded %zu bytes from %s at 0x%08x\n",
           image.size, filename, image.load_addr);
    return true;
}
