#include <am.h>
#include <nemu.h>
// 我们对nemu编译得到的可执行文件的行为进行简单的梳理:

// 第一条指令从abstract-machine/am/src/$ISA/nemu/start.S开始, 设置好栈顶之后就跳转到abstract-machine/am/src/platform/nemu/trm.c的_trm_init()函数处执行.
// 在_trm_init()中调用main()函数执行程序的主体功能, main()函数还带一个参数, 目前我们暂时不会用到, 后面我们再介绍它.
// 从main()函数返回后, 调用halt()结束运行.

extern char _heap_start;
int main(const char *args);

Area heap = RANGE(&_heap_start, PMEM_END);
static const char mainargs[MAINARGS_MAX_LEN] = TOSTRING(MAINARGS_PLACEHOLDER); // defined in CFLAGS

void putch(char ch) {
  outb(SERIAL_PORT, ch);
}

void halt(int code) {
  nemu_trap(code);

  // should not reach here
  while (1);
}

void _trm_init() {
  int ret = main(mainargs);
  halt(ret);
}
