#include "device/device.h"

// A deterministic device model for AXI handshake tests. Read queries expose how
// often the DPI callbacks ran, so a stalled response cannot hide duplicate I/O.
static uint32_t reads = 0;
static uint32_t writes = 0;
static uint32_t last_addr = 0;
static uint32_t last_data = 0;
static uint8_t last_mask = 0;

extern "C" uint32_t mmio_read(uint32_t addr) {
    switch (addr) {
        case 0xa0000048: return ++reads;
        case 0xa000004c: return writes;
        case 0xa0000050: return last_addr;
        case 0xa0000054: return last_data;
        case 0xa0000058: return last_mask;
        default: return 0;
    }
}

extern "C" void mmio_write(uint32_t addr, uint32_t data, uint8_t mask) {
    ++writes;
    last_addr = addr;
    last_data = data;
    last_mask = mask;
}
