# NEMU

NEMU(NJU Emulator) is a simple but complete full-system emulator designed for teaching purpose.
Currently it supports x86, mips32, riscv32 and riscv64.
To build programs run above NEMU, refer to the [AM project](https://github.com/NJU-ProjectN/abstract-machine).

The main features of NEMU include
* a small monitor with a simple debugger
  * single step
  * register/memory examination
  * expression evaluation without the support of symbols
  * watch point
  * differential testing with reference design (e.g. QEMU)
  * snapshot
* CPU core with support of most common used instructions
  * x86
    * real mode is not supported
    * x87 floating point instructions are not supported
  * mips32
    * CP1 floating point instructions are not supported
  * riscv32
    * only RV32IM
  * riscv64
    * only RV64IM
* memory
* paging
  * TLB is optional (but necessary for mips32)
  * protection is not supported
* interrupt and exception
  * protection is not supported
* 5 devices
  * serial, timer, keyboard, VGA, audio
  * most of them are simplified and unprogrammable
* 2 types of I/O
  * port-mapped I/O and memory-mapped I/O









# 2 开发日志


## 2.1 通过Kconfig系统添加功能.

Kconfig简介:

-   **Kconfig (菜单定义)**：它是“说明书”，规定了有哪些开关、长什么样、依赖谁。  
-   **`.config` (用户的选择)**：它是“订单”，记录了到底开了哪个、关了哪个。  
-   **Makefile / C Code (执行者)**：它们是“厨师”，根据订单来炒菜。

---

### 首先编辑Kconfig, 也就是编辑GUI.

### kconfig对应程序在`nemu/tools/kconfig`中, 但是nemu/Makefile已经写好了, 你要做的只用`cd ./nemu && make menuconfig`, 就可以调用程序打开GUI开始设置.

### 选完之后退出, 这个程序会在退出的时候生成文件:
-   **生成.config配置文件**在`./nemu/.config`. 这个`.config`里面就是一行行的`CONFIG_XXX`变量. **这个文件可以人看, 也用它生成下面三个文件.**
-   **可以被包含到C代码中的 宏定义文件**(`nemu/include/generated/autoconf.h`), 这些宏的名称都是形如`CONFIG_xxx`的形式. **它被`common.h`包含**, `common.h`被几乎所有源代码的.h包含. 所以编译nemu可执行模拟器文件时, 已经自动包含了`autoconf.h`
-   **可以被包含到Makefile中的 变量定义文件**(`nemu/include/config/auto.conf`). **它已经被`nemu/Makefile`包含**, 这样Makefile中就可以使用`CONFIG_XXX`变量了.
-   **可以被包含到Makefile中的, 依赖关系**(`nemu/include/config/auto.conf.cmd`). **它也被`nemu/Makefile`包含**.


### 这就是配置完了. 接下来你写代码的时候, 在Makefile中以及在C程序中使用对应的宏来实现你想要的效果.


## 2.2 MTRACE   思想: 在项目编译过程中, 通过Makefile实现字符的预处理

首先在nemu/Kconfig中添加`MTRACE`等四个设计好的配置选项代码. 配置选项`MTRACE`的打开, 改变对应的宏`CONFIG)MTRACE`. 同时`Kconfig`生成`.config`文件, 其中生成: 

```Kconfig
# ============mtrace功能. 开启后可记录内存读写操作=============
# 编译时开关: 是否将paddr.c内有关mtrace实现的代码参与编译. 否则会被宏注释.
config MTRACE
  depends on TRACE
  bool "Enable memory access tracer"
  default n
  help
    Enable memory access tracing to record read/write operations.
    Useful for debugging memory-related bugs.
# 运行时开关: 是否记录日志.
config MTRACE_COND
  depends on MTRACE
  string "Only trace memory access when the condition is true"
  default "true"
# 是否开启范围过滤
config MTRACE_RANGE_ENABLE
  depends on MTRACE
  bool "Enable memory trace range filter"
  default n
  help
    Only trace memory accesses within a specific address range.
# 地址范围设定
config MTRACE_RANGE_START
  depends on MTRACE_RANGE_ENABLE
  hex "Memory trace range start address"
  default 0x80000000
config MTRACE_RANGE_END
  depends on MTRACE_RANGE_ENABLE
  hex "Memory trace range end address"
  default 0x88000000
```


但是注意一个特殊点: `CONFIG_MTRACE_COND`是一个**字符串类型**的配置选项, 其值是一个条件表达式. 这个条件表达式会被用在代码中作为判断是否记录日志的条件. 但是Kconfig生成的`autoconf.h`中, 宏`CONFIG_MTRACE_COND`的值是带引号的字符串, 例如`"true"`或者`"$addr >= 0x80000000 && $addr < 0x88000000"`. 

我们想要在C中使用一个表达式. 所以需要一个`CONFIG_MTRACE_COND`的没有引号的版本. 

这在C文件中很难实现, 因为预处理做不到字符处理.

只好在Makefile中实现:

在nemu/Makefile中, `# =====================Extract compiler and options from menuconfig=========================`位置,
引入去掉括号版本的宏, 称为`CONFIG_MTRACE_COND_EXPR`:

```Makefile
# =====================Extract compiler and options from menuconfig=========================
#...
CFLAGS_TRACE += -DCONFIG_ITRACE_COND_EXPR=$(if $(CONFIG_ITRACE_COND),$(call remove_quote,$(CONFIG_ITRACE_COND)),true) 

```

然后在源代码实现功能的部分使用`CONFIG_MTRACE_COND_EXPR`即可.


接下来就是具体实现, 在源代码`paddr.c`中编写相应功能, 并且用宏
* `CONFIG_MTRACE`
* `CONFIG_MTRACE_COND_EXPR`
* `CONFIG_MTRACE_RANGE_ENABLE`
* `CONFIG_MTRACE_RANGE_START`
* `CONFIG_MTRACE_RANGE_END`
来实现功能即可.

具体就是在`paddr_read`和`paddr_write`两个函数中添加mtrace逻辑即可.

fin.



## 2.3 nemu用作REF:


![alt text](image.png)














