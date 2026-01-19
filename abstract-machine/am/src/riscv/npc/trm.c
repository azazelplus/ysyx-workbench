
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
// 执行两条指令: 1. 将main的退出码放到a0寄存器中. 2. 执行ebreak指令.
// EBREAK指令本身不带立即数, 仿真器/硬件需要检查a0寄存器的值来确定程序是否成功退出(0为成功).
void halt(int code) {
    asm volatile(
        "mv a0, %0; ebreak"     //汇编指令. %0表示第一个操作数(从下面的 输出操作数 开始编号, 然后是输入操作数)
        :               //输出操作数(riscv中为rd)
        :"r"(code));    //输入操作数(riscv中为rs1,rs2). "r"(code)表示: 操作数值来自C变量code, r表示要求将其放入一个通用寄存器中. 也就是要求%0选用一个通用寄存器.
    while (1);
}

// 这是main()函数入口. start.S会调用它.
void _trm_init() {
  int ret = main(mainargs);
  halt(ret);
}
