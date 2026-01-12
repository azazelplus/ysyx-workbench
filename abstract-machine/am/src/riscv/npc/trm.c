
// terminal interface for riscv npc, 终端接口. 提供putch, halt, 串口.
// 

#include <am.h>
#include <klib-macros.h>

// 堆区间定义.
extern char _heap_start;
int main(const char *args);

extern char _pmem_start;
#define PMEM_SIZE (128 * 1024 * 1024)
#define PMEM_END  ((uintptr_t)&_pmem_start + PMEM_SIZE)

// 串口的内存映射地址.
#define SERIAL_PORT 0xa00003f8


// 堆区间定义.
Area heap = RANGE(&_heap_start, PMEM_END);
static const char mainargs[MAINARGS_MAX_LEN] = TOSTRING(MAINARGS_PLACEHOLDER); // defined in CFLAGS


// printf的底层实现. am库提供的printf函数会调用putch来实现打印到终端.
// 硬件要求: inirv 必须实现 MMIO (内存映射 I/O).
void putch(char ch) {
    *(volatile char *)SERIAL_PORT = ch;
}

// 停机指令.
// 硬件要求: minirv需要实现ebreak
void halt(int code) {
    asm volatile("mv a0, %0; ebreak" : :"r"(code));
    while (1);
}

// 这是C程序入口. start.S会调用这个它.
void _trm_init() {
  int ret = main(mainargs);
  halt(ret);
}
