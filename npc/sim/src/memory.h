#ifndef NPC_MEMORY_H
#define NPC_MEMORY_H

#include <cstddef>
#include <cstdint>

// 当前 flat binary 从 RAM 起点装载；物理地址、大小与镜像字节一起交给 DiffTest。
struct ProgramImage {
    uint32_t load_addr;
    size_t size;
    const uint8_t *data;
};

bool memory_load_image(const char *filename, ProgramImage &image);
bool memory_contains(uint32_t addr, size_t size);
// 调用方先确认完整的四字节范围有效；数据按 RISC-V 小端序排列。
uint32_t memory_read_word(uint32_t addr);
void memory_write_word(uint32_t addr, uint32_t data, uint8_t mask);

#endif
