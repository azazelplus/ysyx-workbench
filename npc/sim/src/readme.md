这里存放着参与编译 仿真程序 的源码.

sim.mk中的通配符会保证所有src下的.c, .cpp, .cc, .h, .hpp, .hh文件都会参与编译.

但是, 由于verilator会生成自动的`VMinRV.mk`, 它会对所有参与编译的.C源码, 提取其中的`include path`, 自动拼接为`src/path`

所以必须维护src下的include路径, 给出完整的src相对路径. 比如`#include "utils/disasm.h"`.




main.cpp: 仿真主程序
config.h: 仿真功能开关与默认参数
cpu/cpu.h/cpp: CPU 执行控制、时钟推进、复位和波形
cpu/difftest/difftest.h/cpp: 加载 NEMU 参考模型、初始化状态、逐条比较
monitor/sdb/sdb.h/cpp: 调试器命令与交互循环
memory/memory.h/cpp: 镜像装载、绑定硬件 RAM、物理地址转换和字节掩码读写
memory/ram.h/cpp: 取得 Verilator 中 RAM 存储阵列的指针，交给 memory.cpp
utils/disasm.h/cpp: 反汇编模块, 将机器码转换为汇编字符串
trace/itrace.h/cpp: 指令跟踪模块, 用于打印每条指令
trace/mtrace.h/cpp: 内存访问跟踪
trace/ftrace.h/cpp: 函数调用跟踪
trace/dtrace.h/cpp: 设备访问跟踪 (串口/RTC/MMIO)
dpi/dpic.cpp: DPI-C 接口, 寄存器同步、TRAP 和 RAM 访问追踪；保留原 PMEM 接口供对照
device/device.h/cpp: UART / RTC 的 MMIO 设备实现

头文件与实现放在同一职责目录中，跨模块引用使用相对于 sim/src 的路径，
例如 `#include "memory/memory.h"`、`#include "cpu/difftest/difftest.h"`。
sim.mk 递归收集源文件，增加上述目录内的 .cpp 文件无需逐个登记。

程序镜像的装载顺序：
cpu_init 创建并复位 DUT → ram_bind 绑定硬件存储阵列
→ memory_load_image 清零 RAM 并将 .bin 直接读入 RAM → cpu_exec 开始执行。
运行期间，CPU 经 Arbiter → Xbar → RAM.scala 的 AXI 接口访问内存。
SDB 的 memory_read_word 直接查看同一个存储阵列；开启 DiffTest 时，在 CPU 开始执行前从 RAM 复制初始镜像到 NEMU。


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





## npc的时钟:

在 `abstract-machine/scripts/platform/npc.mk` 中设定，AM 驱动和 DPI 实现共同使用：
```makefile
NPC_RTC_BASE := 0xa0000048
```


## npc的串口:

在 `abstract-machine/scripts/platform/npc.mk` 中设定：
```makefile
NPC_UART_BASE := 0x10000000
```

完整的地址职责、构建流程和 RAM 接入说明见 [平台与内存说明](../../docs/platform.md)。


