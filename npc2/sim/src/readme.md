这里存放着参与编译 仿真程序 的源码.


如果想添加功能, 记得在sim.mk中将添加的源码参与到编译中...

main.cpp: 仿真主程序
disasm.h/cpp: 反汇编模块, 将机器码转换为汇编字符串
trace/itrace.h/cpp: 指令跟踪模块, 用于打印每条指令
trace/mtrace.h/cpp: 内存访问跟踪
trace/ftrace.h/cpp: 函数调用跟踪
trace/dtrace.h/cpp: 设备访问跟踪 (串口/RTC/MMIO)
dpic.cpp: DPI-C 接口, 硬件-软件协作仿真的存储器和 MMIO 操作


=== DTRACE (设备访问跟踪) 功能 ===

trace/dtrace.cpp 中实现了 DTRACE (Device TRACe) 功能，用于记录所有 MMIO 设备的读写操作。

【编译时配置】
在 sim/sim.mk 中:
```makefile
DTRACE_FLAGS += -DENABLE_DTRACE=1                                    # 启用 dtrace
DTRACE_FLAGS += -DCONFIG_DTRACE_COND_EXPR=$(if $(DTRACE_COND),...) # 设置条件表达式
```

【运行时使用】
1. 启用所有 dtrace 输出 (默认):
   make sim
   make run IMG=prog.bin

2. 条件输出 (仅在特定周期输出):
   make sim DTRACE_COND="(g_cycle > 1000 && g_cycle < 2000)"
   make run IMG=prog.bin

3. 关闭 dtrace:
   make sim DTRACE_FLAGS=""
   make run IMG=prog.bin

【输出格式示例】
[DTRACE] READ  RTC: addr=0xa0000048, data=0x000012ab
[DTRACE] WRITE SERIAL: addr=0x10000000, data=0x48 ('H')
[DTRACE] WRITE  MEM: addr=0x80000100, data=0xdeadbeef, mask=0x0f





## npc2的时钟:

首先在dpic.cpp中设定:
```c
#define RTC_ADDR    0xa0000048UL
```


## npc2的串口:

在trm.c中:`
```c
// 串口的内存映射地址.
#define SERIAL_PORT 0x10000000
```


