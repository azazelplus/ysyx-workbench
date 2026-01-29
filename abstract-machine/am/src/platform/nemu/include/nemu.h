//这里是nemu平台相关的 ISA通用的 (xxx-nemu) 输入输出工具代码.
#ifndef NEMU_H__
#define NEMU_H__

#include <klib-macros.h>

#include ISA_H // the macro `ISA_H` is defined in CFLAGS
               // it will be expanded as "x86/x86.h", "mips/mips32.h", ...

#if defined(__ISA_X86__)
# define nemu_trap(code) asm volatile ("int3" : :"a"(code))
#elif defined(__ISA_MIPS32__)
# define nemu_trap(code) asm volatile ("move $v0, %0; sdbbp" : :"r"(code))
#elif defined(__riscv)
# define nemu_trap(code) asm volatile("mv a0, %0; ebreak" : :"r"(code))
#elif defined(__ISA_LOONGARCH32R__)
# define nemu_trap(code) asm volatile("move $a0, %0; break 0" : :"r"(code))
#else
# error unsupported ISA __ISA__
#endif

#if defined(__ARCH_X86_NEMU)
# define DEVICE_BASE 0x0
#else
// riscv等架构的NEMU平台, MMIO设备映射在高地址空间.
# define DEVICE_BASE 0xa0000000
#endif

// 对riscv(只支持内存映射)来说, MMIO_BASE即DEVICE_BASE.
// 对x86来说, MMIO_BASE是专门给帧缓冲区和音频缓冲区预留的地址空间.
#define MMIO_BASE 0xa0000000

// 外设地址定义
#define SERIAL_PORT     (DEVICE_BASE + 0x00003f8) //riscv时, 对应内存地址 0xa00003f8
#define KBD_ADDR        (DEVICE_BASE + 0x0000060)
#define RTC_ADDR        (DEVICE_BASE + 0x0000048) //RTC时钟的地址
#define VGACTL_ADDR     (DEVICE_BASE + 0x0000100)
#define AUDIO_ADDR      (DEVICE_BASE + 0x0000200)
#define DISK_ADDR       (DEVICE_BASE + 0x0000300)
#define FB_ADDR         (MMIO_BASE   + 0x1000000) //FB=Frame Buffer, 帧缓冲区地址.
#define AUDIO_SBUF_ADDR (MMIO_BASE   + 0x1200000) //音频缓冲区地址.

// _pmem_start来自...
extern char _pmem_start;
#define PMEM_SIZE (128 * 1024 * 1024) // 128MB物理内存.
#define PMEM_END  ((uintptr_t)&_pmem_start + PMEM_SIZE) // 
#define NEMU_PADDR_SPACE \
  RANGE(&_pmem_start, PMEM_END), \
  RANGE(FB_ADDR, FB_ADDR + 0x200000), \
  RANGE(MMIO_BASE, MMIO_BASE + 0x1000) /* serial, rtc, screen, keyboard */

typedef uintptr_t PTE;

#define PGSIZE    4096  //页大小: 4KB

#endif
