// 只需要实现很少的API就可以支撑起程序在TRM上运行!

#include <am.h>
#include <nemu.h>

extern char _heap_start;
int main(const char *args);

//指示堆区的起始和末尾
Area heap = RANGE(&_heap_start, PMEM_END);
#ifndef MAINARGS
#define MAINARGS ""
#endif
static const char mainargs[] = MAINARGS;

// 输出一个字符
void putch(char ch) {
  outb(SERIAL_PORT, ch);
}

// 结束程序的运行
void halt(int code) {
  nemu_trap(code);

  // should not reach here
  while (1);
}

// 进行TRM相关的初始化工作
void _trm_init() {
  int ret = main(mainargs);
  halt(ret);
}
