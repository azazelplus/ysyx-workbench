#include <am.h>
#include <stdio.h>
#include <klib-macros.h>

// trm = 图灵机. 提供最基础的运行时环境, 包括输出能力(putch)和停机能力(halt). 其他功能如中断/异常处理, 由cte.c实现. 

void __am_platform_dummy();
void __am_exit_platform(int code);

void trm_init() {
  __am_platform_dummy();
}

// 调用C标准库的putchar()
void putch(char ch) {
  putchar(ch);
}

// 停机指令. 接收code作为退出码.
void halt(int code) {

  //解析code的hex格式, 打印到终端.
  const char *fmt = "Exit code = 40h\n";
  for (const char *p = fmt; *p; p++) {
    char ch = *p;
    if (ch == '0' || ch == '4') {
      ch = "0123456789abcdef"[(code >> (ch - '0')) & 0xf];
    }
    putch(ch);
  }

  __am_exit_platform(code); //linux特有函数. 调用exit(code).
  putstr("Should not reach here!\n");
  while (1);
}

Area heap = {};
