//
#ifndef RISCV_H__
#define RISCV_H__

#include <stdint.h>

// ============= MMIO / 内存映射 IO 访问宏 =============
// 使用示例：
//   读取计时器值：    uint32_t time = inl(RTC_ADDR);
//   向串口写字符：    outb(SERIAL_PORT, 'A');

// in*() 函数族：从指定地址读取数据
//   - inb: Input Byte   - 读取 8 位数据 (1 字节)
//   - inw: Input Word   - 读取 16 位数据 (2 字节)
//   - inl: Input Long   - 读取 32 位数据 (4 字节)
static inline uint8_t  inb(uintptr_t addr) { return *(volatile uint8_t  *)addr; }
static inline uint16_t inw(uintptr_t addr) { return *(volatile uint16_t *)addr; }
static inline uint32_t inl(uintptr_t addr) { return *(volatile uint32_t *)addr; }

// out*() 函数族：向指定地址写入数据
//   - outb: Output Byte  - 写入 8 位数据 (1 字节)
//   - outw: Output Word  - 写入 16 位数据 (2 字节)
//   - outl: Output Long  - 写入 32 位数据 (4 字节)
static inline void outb(uintptr_t addr, uint8_t  data) { *(volatile uint8_t  *)addr = data; }
static inline void outw(uintptr_t addr, uint16_t data) { *(volatile uint16_t *)addr = data; }
static inline void outl(uintptr_t addr, uint32_t data) { *(volatile uint32_t *)addr = data; }

// ============ 页表项 (PTE) 和 内存保护位定义 ============
// PTE 位定义:
#define PTE_V 0x01  // Valid: 有效
#define PTE_R 0x02  // Read: 允许读取
#define PTE_W 0x04  // Write: 允许写入
#define PTE_X 0x08  // Execute: 允许执行
#define PTE_U 0x10  // User: 允许用户态访问
#define PTE_A 0x40  // Accessed: 被访问过
#define PTE_D 0x80  // Dirty: 被写入过


// =============== 第三部分: CPU 特权模式和状态寄存器定义 ===============

enum { MODE_U, MODE_S, MODE_M = 3 }; // RISC-V 的三个等级: U(用户), S(监督者/内核), M(机器/最高权限)

// mstatus 寄存器的功能位
#define MSTATUS_MXR  (1 << 19)  // Make Executable Readable: 允许把“可执行”页当成“可读”页
#define MSTATUS_SUM  (1 << 18)  // permit Supervisor User Memory: 允许内核直接访问用户态的内存

#if __riscv_xlen == 64
#define MSTATUS_SXL  (2ull << 34)   // 设置 S-mode 的位数 (32/64位) 喵
#define MSTATUS_UXL  (2ull << 32)   // 设置 U-mode 的位数 (32/64位) 喵
#else
#define MSTATUS_SXL  0
#define MSTATUS_UXL  0
#endif

#endif
