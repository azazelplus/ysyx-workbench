
AbstractMachine is a minimal, modularized, and machine-independent 
abstraction layer of the computer hardware:

* physical memory and direct execution (The "Turing Machine");
* basic model for input and output devices (I/O Extension);
* interrupt/exception and processor context management (Context Extension);
* virtual memory and protection (Virtual Memory Extension);
* multiprocessing (Multiprocessing Extension).

CONTACTS

Bug reports and suggestions go to Yanyan Jiang (jyy@nju.edu.cn) and Zihao 
Yu (yuzihao@ict.ac.cn).





# 1. ...










AM 的核心思想是把“程序运行的环境”抽象成的一组 API。

任何实现了这组 API 的东西(无论是真实的硬件 minirv, 还是模拟器 nemu, 甚至是 Linux 进程 native),

对于上层的 AM 应用程序来说都是一样的。

AM = TRM + IOE + CTE + VME + MPE

-   TRM(Turing Machine) - 图灵机, 最简单的运行时环境, 为程序提供基本的计算能力
-   IOE(I/O Extension) - 输入输出扩展, 为程序提供输出输入的能力
-   CTE(Context Extension) - 上下文扩展, 为程序提供上下文管理的能力
-   VME(Virtual Memory Extension) - 虚存扩展, 为程序提供虚存管理的能力
-   MPE(Multi-Processor Extension) - 多处理器扩展, 为程序提供多处理器通信的能力 (MPE超出了ICS课程的范围, 在PA中不会涉及)


am-kernels子项目用于收录一些可以在AM上运行的测试集和简单程序.

```
am-kernels
├── benchmarks                  # 可用于衡量性能的基准测试程序
│   ├── coremark
│   ├── dhrystone
│   └── microbench
├── kernels                     # 可展示的应用程序
│   ├── hello
│   ├── litenes                 # 简单的NES模拟器
│   ├── nemu                    # NEMU
│   ├── slider                  # 简易图片浏览器
│   ├── thread-os               # 内核线程操作系统
│   └── typing-game             # 打字小游戏
└── tests                       # 一些具有针对性的测试集
    ├── am-tests                # 针对AM API实现的测试集
    └── cpu-tests               # 针对CPU指令实现的测试集
```


# 2. 调用分析: 

## makefile编译阶段

运行cpu-tests/下的`make ARCH=minirv-npc2 ALL=dummy run`后:

* 生成临时mk文件`Makefile.dummy`并运行, 它传入参数SRC和ARCH, 调用am的主Makefile, 即`abstract-machine/Makefile`.

* 主Makefile 根据 ARCH 变量引入对应的架构 mk 文件.
  * 第一步：`-include $(AM_HOME)/scripts/$(ARCH).mk`（即 `scripts/minirv-npc2.mk`）
  * 第二步：`minirv-npc2.mk` 再内部 include ISA 和 Platform 的 mk 文件：
    * `include $(AM_HOME)/scripts/isa/riscv.mk`
    * `include $(AM_HOME)/scripts/platform/npc2.mk`
  * 第三步：主 Makefile 继续执行编译规则，生成 IMAGE 文件（ELF 和 BIN）. 最终生成物为`dummy-minirv-npc2.bin`.
    * 参与编译的文件:
      * am/src/riscv/npc 下的 am 接口实现源码, 包括:
        * `start.S`
        * `trap.S`
        * `cte.c`
        * `vme.c`
        * `trm.c` 提供halt()函数
      * klib 库源码
      * cpu-tests/tests 下的应用程序源码（如 `dummy.c`）

* 当主 Makefile 遇到 `run` 目标时，它会调用 `platform/npc2.mk` 中定义的 `run` 规则：
  * `platform/npc2.mk` 定义了 `run: insert-arg` 规则，它执行： `$(MAKE) -C $(NPC2_HOME) run IMG=$(IMAGE).bin`
  * 这个命令进入 `npc2` 项目，调用 `npc2/Makefile` 的 `run` 规则。
  * `npc2/Makefile` 的 `run` 规则是：`include sim/sim.mk`，因此最终在 `npc2/sim/sim.mk` 中执行仿真。


* 于是运行sim.mk, 且传入参数IMG, MAX_CYCLES(可选):
  * 设定仿真周期MAX_CYCLES ?= 100000
  * 执行mill命令来编译scala源码以生成verilog.
  * 编译仿真可执行程序`/npc2/build/obj_dir/VMiniRV`
    * 参与编译`VMiniRV`的源码: 
      * `main.cpp`
      * `npc2/src/main/scala/*.scala`
  * 运行可执行程序`VMiniRV`, 传入参数: IMG, MAX_CYCLES(可选).  仿真开始.

##　仿真程序`VMINIRV`运行阶段

* 上电：`VMiniRV`(Verilator 仿真执行文件)在宿主机(wsl)上启动时:
  * 宿主机 (wsl) 的 `main.cpp` 打开并读取 `dummy-minirv-npc2.bin` 文件: 
  ```cpp
  long prog_size = load_program(argv[1]);
  ```
  * 将`dummy-minirv-npc2.bin`二进制内容加载到 **NPC 虚拟硬件的物理内存** 地址 `0x80000000`（这个内存由 PMEM C++ 模块使用全局数组 `pmem[]` 模拟）
  * 将 NPC 虚拟 PC 寄存器设置为 `0x80000000`
  * 启动仿真循环：逐个时钟周期调用 Verilator 生成的 `eval()` 函数，驱动 Scala/Chisel 生成的硬件电路运行 RISC-V 指令

### 二进制程序`dummy-minirv-npc2.bin`在npc2上运行阶段
  * 启动：
    * `0x80000000`: `start.S` 开始执行。
    * 设置完堆栈后，`call _trm_init`。

  * AM 初始化 (`trm.c`), 执行`main`函数, 返回值给`halt()`:
    * `_trm_init()` 被调用. **它是 测试C程序 main函数的入口**. 它调用`main()` (即 `dummy.c` 编译后的代码), 执行测试逻辑. `main` 返回后, 其返回值(exit code)被传递给 `halt()`.
    * halt函数执行两条汇编指令死循环等待.
      * 1. 将返回值存入寄存器a0
      * 2. 执行ebreak指令.


```cpp
void _trm_init() {
  int ret = main(mainargs);
  halt(ret);
}
void halt(int code) {
  asm volatile(
      "mv a0, %0; ebreak"     //汇编指令. %0表示第一个操作数(从下面的 输出操作数 开始编号, 然后是输入操作数)
      :               //输出操作数(riscv中为rd)
      :"r"(code));    //输入操作数(riscv中为rs1,rs2). "r"(code)表示: 操作数值来自C变量code, r表示要求将其放入一个通用寄存器中. 也就是要求%0选用一个通用寄存器.
  while (1);
}
```

  * ebreak指令自杀:
    * npc2 收到 ebreak 指令. `EBREAKDetect` 模块检测到当前指令是ebreak后, 调用 DPI-C 接口 `ebreak_handler()`。

  * 仿真器介入 (`main.cpp` 中的 `ebreak_handler()`): 读取 `cpu_regs[10]` (即 a0 寄存器) 的值作为程序退出码, 执行exit(a0)退出仿真.
```cpp
void ebreak_handler() {
    int exit_code = (int)cpu_regs[10];
    if(exit_code == 0) printf("HIT GOOD TRAP");
    else printf("HIT BAD TRAP");
    exit(exit_code);
}
```
* C++ 进程以该退出码终止仿真，AM 得到测试结果。



---


---

**核心概念澄清：**

| 层级 | 内容 | 所有者 |
|------|------|--------|
| **宿主机 (Host)** | `VMiniRV`（C++ 仿真器）、`main.cpp`、PMEM 数组 | Linux PC |
| **虚拟硬件 (Guest)** | NPC CPU 寄存器、PC、虚拟内存（由 PMEM[] 数组模拟） | Verilator 模拟 |
| **虚拟内存内容** | `dummy-minirv-npc2.bin`（RISC-V 机器码） | 加载到虚拟地址 0x80000000 |

PMEM（Physical Memory）是 C++ 中的一个全局数组，它代表"NPC 虚拟硬件看到的内存"。当 NPC 执行 `lw` 指令时，实际上是在访问这个 C++ 数组；当执行 `sw` 指令时，实际上是在修改这个数组的内容。




---

# 3. AM 源代码结构详解

## 为什么会有多套重复的 trm.c, cte.c 等文件？

AM 采用**多层继承/覆盖设计**，原因是：

### 1. 多 ISA 支持
不同的指令集架构（x86、RISC-V、MIPS）需要不同的**汇编代码实现**（如 `start.S`, `trap.S`）。
- `src/riscv/` - RISC-V 相关的实现（汇编代码必须用 RISC-V 指令）
- `src/x86/` - x86 相关的实现（汇编代码必须用 x86 指令）
- `src/mips/` - MIPS 相关的实现
- `src/native/` - Linux 进程的实现（直接用 C 调用 Linux API，不需要汇编）

### 2. 多平台支持（同一 ISA，不同平台）
同一 ISA 可以在不同的**模拟器或硬件平台**上运行，需要不同的初始化和 I/O 实现：
- `src/riscv/nemu/` - RISC-V 在 NEMU 上的实现
- `src/riscv/npc2/` - RISC-V 在 NPC2 上的实现（您的项目）
- `src/riscv/spike/` - RISC-V 在 Spike 上的实现
- `src/x86/qemu/` - x86 在 QEMU 上的实现
- `src/x86/nemu/` - x86 在 NEMU 上的实现

### 3. 编译时的文件选择逻辑

编译时通过 `ARCH` 变量**自动选择对应的实现**：

```makefile
# 当你执行：
make ARCH=minirv-npc2 ALL=dummy

# AM 的 Makefile 会：
# 1. 通过 ARCH 变量 (minirv-npc2) 引入 scripts/minirv-npc2.mk
# 2. minirv-npc2.mk 内部指定 ISA (riscv) 和 Platform (npc2)
# 3. 编译时只选择 src/riscv/npc2/ 下的文件，不选择其他的
```

### 4. 文件继承关系示例

以 `halt()` 函数为例：

| 文件位置 | 内容 | 使用场景 |
|---------|------|--------|
| `src/riscv/npc2/trm.c` | `halt()` 实现：`mov a0, code; ebreak` | NPC2 平台 |
| `src/riscv/nemu/trm.c` | `halt()` 实现：`mov a0, code; ebreak` | NEMU 平台 |
| `src/x86/nemu/trm.c` | `halt()` 实现：`mov eax, code; hlt` | x86 NEMU 平台 |
| `src/native/trm.c` | `halt()` 实现：直接调用 Linux 的 `exit(code)` | 本地 Linux |

**关键点**：各平台的 `halt()` 实现完全不同，因为不同的 ISA 有不同的汇编指令，不同的平台有不同的停机机制。

### 5. 目录结构图

```
src/
├── native/                    # Linux 进程环境 (不需要汇编)
│   ├── trm.c                 # exit(code) 实现
│   ├── cte.c
│   ├── vme.c
│   └── ...
├── riscv/                     # RISC-V ISA (所有平台通用的汇编代码在这里)
│   ├── start.S               # 程序入口 (RISC-V 汇编)
│   ├── trap.S                # 异常处理 (RISC-V 汇编)
│   ├── nemu/                 # NEMU 平台特定
│   │   ├── trm.c             # halt() via ebreak
│   │   ├── cte.c
│   │   └── vme.c
│   └── npc2/                 # NPC2 平台特定
│       ├── trm.c             # halt() via ebreak
│       ├── cte.c
│       └── vme.c
├── x86/                       # x86 ISA
│   ├── x86.h
│   ├── nemu/
│   │   ├── start.S           # x86 汇编入口
│   │   ├── trm.c             # halt() via hlt
│   │   └── ...
│   └── qemu/
│       ├── start.S
│       ├── trm.c
│       └── ...
└── mips/
    └── nemu/
        └── ...
```

### 6. 编译时的文件搜索顺序

当 Makefile 需要找 `trm.c` 时，按照这样的优先级：

```
1. src/$(ARCH_ISA)/$(ARCH_PLATFORM)/trm.c  # 最优先 (平台特定)
   例如：src/riscv/npc2/trm.c

2. src/$(ARCH_ISA)/trm.c                   # 其次 (ISA 通用)
   例如：src/riscv/trm.c (如果存在的话)

3. src/platform/$(ARCH_PLATFORM)/trm.c    # 再其次 (某些通用平台实现)
   例如：src/platform/nemu/trm.c
```

**这就是为什么有多个 `trm.c`！每个文件服务于特定的 ISA+Platform 组合。**

---

abstract-machine/目录下的源文件组织如下:


abstract-machine
├── am                                  # AM内核
│   ├── include
│   │   ├── amdev.h
│   │   ├── am.h                        # 列出了AM中的所有API.
│   │   └── arch                        # 架构相关的头文件定义
│   ├── Makefile
│   └── src                             #源码
│       ├── mips
│       │   ├── mips32.h
│       │   └── nemu                    # mips32-nemu相关的实现
│       ├── native
│       ├── platform                    # `/src/platform` 存放那些通用平台的am握手实现. 比如nemu(支持minirv, riscv32, x86...)
│       │   └── nemu                    # 以NEMU为平台的AM实现
│       │       ├── include
│       │       │   └── nemu.h
│       │       ├── ioe                 # IOE
│       │       │   ├── audio.c
│       │       │   ├── disk.c
│       │       │   ├── gpu.c
│       │       │   ├── input.c
│       │       │   ├── ioe.c
│       │       │   └── timer.c
│       │       ├── mpe.c               # MPE, 当前为空
│       │       └── trm.c               
│       ├── riscv                       # `/src/riscv`  存放和riscv相关的am接口实现. 对于npc2(我自己写的minirv), 由于它只能跑riscv32, 所以关于npc2的完整am接口实现都放在`/src/riscv/npc2`.
│       │   ├── nemu                    # riscv32(64)相关的实现
│       │   │   ├── cte.c               # CTE
│       │   │   ├── start.S             # 程序入口
│       │   │   ├── trap.S
│       │   │   └── vme.c               # VME
│       │   └── npc2                     # npc2的完整am接口实现.
│       │       ├── cte.c               # CTE
│       │       ├── start.S             # 程序入口
│       │       ├── trap.S
│       │       └── vme.c               # VME
│       └── x86
│           ├── nemu                    # x86-nemu相关的实现
│           └── x86.h
├── klib                                # 常用函数库. 一些架构无关的库函数, 方便应用程序的开发
├── Makefile                            # 公用的Makefile规则
└── scripts                             # 存放不同ARCH下构建/运行ELF/image的Makefile.
    ├── isa
    │   ├── mips32.mk
    │   ├── riscv32.mk
    │   ├── riscv64.mk
    │   └── x86.mk
    ├── linker.ld                       # 链接脚本
    ├── mips32-nemu.mk
    ├── native.mk
    ├── platform
    │   └── nemu.mk
    ├── riscv32-nemu.mk
    ├── riscv64-nemu.mk
    └── x86-nemu.mk



