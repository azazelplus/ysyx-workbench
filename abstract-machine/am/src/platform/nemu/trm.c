#include <am.h>
#include <nemu.h>
// trm, 图灵机, 最简单的运行时环境, 为程序提供基本的计算能力.
// 我们对nemu编译得到的可执行文件的行为进行简单的梳理:

// 第一条指令从abstract-machine/am/src/$ISA/nemu/start.S开始, 设置好栈顶之后就跳转到abstract-machine/am/src/platform/nemu/trm.c的_trm_init()函数处执行.
// 在_trm_init()中调用main()函数执行程序的主体功能, main()函数还带一个参数, 目前我们暂时不会用到, 后面我们再介绍它.
// 从main()函数返回后, 调用halt()结束运行.

extern char _heap_start;
int main(const char *args);



 
/*堆区.
* Area结构体 在am/include/am.h中定义: typedef struct {void *start, *end;} Area;
* 这里的RANGE宏用来包装两个地址到一个Area结构体, 展开就是:
    Area heap = (Area){
      .start = (void *)&_heap_start,
      .end   = (void *)PMEM_END
    };
* _heap_start来自abstract-machine/am/src/platform/nemu/linker.ld链接脚本中定义的堆起始地址符号:
    _heap_start = ALIGN(0x1000);
* ALIGN(x) = 把“当前位置(location counter)”向上对齐到 x 字节.
*/
Area heap = RANGE(&_heap_start, PMEM_END);


// 定义mainarg的初始化字符串. 其中, MAINARGS_MAX_LEN和MAINARGS_PLACEHOLDER宏定义是scripts/platform/nemu.mk给出的. 在未来, nemu.mk会调用insert-arg.py脚本将真正的命令行参数字符串写入到这个数组里.
static const char mainargs[MAINARGS_MAX_LEN] = TOSTRING(MAINARGS_PLACEHOLDER); // defined in CFLAGS

// putch()会将字符输出到串口`SERIAL_PORT`. 这个变量在`nemu.h`中定义: 
// 29,9: #define SERIAL_PORT     (DEVICE_BASE + 0x00003f8)
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
