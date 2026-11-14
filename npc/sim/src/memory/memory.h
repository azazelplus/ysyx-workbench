#ifndef NPC_MEMORY_H
#define NPC_MEMORY_H

#include <cstddef>
#include <cstdint>

// 当前 flat binary 从 RAM 起点装载；物理地址、大小与镜像字节一起交给 DiffTest。
// data 指向 DUT 的 RAM；DiffTest 在 CPU 开始执行前立即复制初始镜像。
struct ProgramImage {
    uint32_t load_addr;
    size_t size;
    const uint8_t *data;
};

bool memory_load_image(const char *filename, ProgramImage &image);
// 绑定 Verilator 中 RAM 的存储阵列；不再分配另一份 CPU 内存。
bool memory_bind_ram(uint32_t base, size_t words, uint32_t *storage);
void memory_unbind_ram();
bool memory_contains(uint32_t addr, size_t size);
// 调用方先确认完整的四字节范围有效；数据按 RISC-V 小端序排列。
uint32_t memory_read_word(uint32_t addr);
void memory_write_word(uint32_t addr, uint32_t data, uint8_t mask);

#endif
