#include "memory.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

int main() {
    const uint32_t base = NPC_RAM_BASE;
    const uint32_t last = base + NPC_RAM_SIZE - 4;
    assert(memory_contains(base, NPC_RAM_SIZE));
    assert(!memory_contains(base - 1, 4));
    assert(memory_contains(last, 4));
    assert(!memory_contains(last, 5));
    assert(!memory_contains(last + 3, 2));

    // 四个字节 lane 的所有掩码；未选中的字节应当保持原值。
    for (unsigned mask = 0; mask < 16; ++mask) {
        memory_write_word(base, 0x44332211, 0xf);
        memory_write_word(base, 0xddccbbaa, mask);
        uint32_t expected = 0x44332211;
        for (unsigned lane = 0; lane < 4; ++lane) {
            if (mask & (1u << lane)) {
                const uint32_t bits = 0xffu << (lane * 8);
                expected = (expected & ~bits) | (0xddccbbaau & bits);
            }
        }
        assert(memory_read_word(base) == expected);
    }
    memory_write_word(last, 0x12345678, 0xf);
    assert(memory_read_word(last) == 0x12345678);

    char path[] = "/tmp/npc-memory-test-XXXXXX";
    const int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *fp = fdopen(fd, "wb");
    assert(fp);
    const unsigned char bytes[] = {0x13, 0, 0, 0, 0xaa, 0xbb, 0xcc, 0xdd};
    assert(fwrite(bytes, 1, sizeof(bytes), fp) == sizeof(bytes));
    assert(fclose(fp) == 0);
    ProgramImage image{};
    assert(memory_load_image(path, image));
    assert(image.load_addr == base && image.size == sizeof(bytes));
    assert(image.data[4] == 0xaa && image.data[7] == 0xdd);
    assert(memory_read_word(base + 4) == 0xddccbbaa);

    fp = fopen(path, "wb");
    assert(fp && fclose(fp) == 0);
    assert(!memory_load_image(path, image));
    fp = fopen(path, "wb");
    assert(fp && fseek(fp, NPC_RAM_SIZE, SEEK_SET) == 0);
    assert(fputc(0, fp) != EOF && fclose(fp) == 0);
    assert(!memory_load_image(path, image));
    unlink(path);
    puts("memory tests passed");
}
