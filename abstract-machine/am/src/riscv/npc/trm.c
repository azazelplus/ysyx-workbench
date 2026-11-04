/*  TRM Trivial Runtime Environment, 简单运行时环境. 为了让一个普通 C 程序能够在没有操作系统的硬件上顺利开始执行、进行最基本的交互，并正常结束，而事先准备的一小套代码和约定. 

CPU不能直接运行一个C程序, 即使是做好成了bin并加载到了cpu内存的pc复位地址, 因为:
1. 程序的结尾是一条ret指令. ret = jalr x0, 0(ra) 而ra寄存器

包括: 
堆区间定义
串口定义
putch()函数
halt(code)函数, 停机指令.
_trm_init()函数, 是程序的入口


当然, 你完全可以写一个不依赖AM, TRM的裸机程序:
.section .text
.global _start

_start:
    li t0, 10
    li t1, 20
    add t2, t0, t1

loop:
    j loop
只要将这段指令正确放入cpu的内存地址, 让PC初始化指向的是_start, cpu就可以跑了.

但是换成一个main函数
int main(const char *args) {
  return 10 + 20;
}
事情就发生了变化.
首先, cpu不认识main. 所以需要安排启动代码. Start.S调用_trm_init()调用main.
其次, cpu不知道return怎麽处理. 所以需要安排cpu停机. _trm_init()调用halt实现停机.
其次, cpu不知道如何打印字符. 所以需要安排putch(), 指导cpu: 通过设置串口寄存器, 实现打印字符.
其次, cpu不会管理堆栈动态分配内存. 如果是linux这样由内核的OS, 运行C程序会自动帮你管理. 但是现在是裸机, 所以需要start.S准备栈.


*/
#include <am.h>
#include <klib-macros.h>
#include "include/platform.h"

// 堆区间定义.
extern char _heap_start;
int main(const char *args);     // 声明main函数(). 因为要在下面的_trm_init()函数中调用main.

// 堆区间定义.
Area heap = RANGE(&_heap_start, PMEM_END);
static const char mainargs[MAINARGS_MAX_LEN] = TOSTRING(MAINARGS_PLACEHOLDER); // defined in CFLAGS


// printf的底层实现. KLIB库提供的printf() 会调用 putch() 来实现打印到终端.
// 硬件要求: inirv 必须实现 MMIO (内存映射 I/O).
void putch(char ch) {
    *(volatile char *)SERIAL_PORT = ch;
}

// 停机指令.
// 执行两条指令: 1. `mv a0, code; ebreak`, 将main的退出码放到a0寄存器中. 2. 执行ebreak指令.
// EBREAK指令本身不带立即数, 仿真器/硬件需要检查a0寄存器的值来确定程序是否成功退出(0为成功).
void halt(int code) {
    asm volatile(
        "mv a0, %0; ebreak"     //汇编指令. %0表示第一个操作数(从下面的 输出操作数 开始编号, 然后是输入操作数)
        :               //输出操作数(riscv中为rd. 这里mv指令没有rd.)
        :"r"(code));    //输入操作数(riscv中为rs1,rs2). "r"(code)表示: 操作数值来自C变量code, r表示要求将其放入一个通用寄存器中. 也就是要求%0选用一个通用寄存器.
    while (1);
}

/* 这是客户程序的入口. cpu上电后, PC指向0x8000 0000, 也就是_start start.S会调用它.
当然, 你也可以在Start.S中自行准备参数, 调用main+处理返回, 这样就可以替换_trm_init()了.
*/
void _trm_init() {
  int ret = main(mainargs); //调用main
  halt(ret);    //停机
}
