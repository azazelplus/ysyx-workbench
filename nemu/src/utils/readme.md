# NEMU Utils 工具模块

src/utils/ 目录包含了 NEMU 模拟器所需的各种工具函数和基础设施。

## 主要模块

### 1. disasm.cc - 指令反汇编

基于 LLVM 项目实现的轻量级反汇编工具。

**主要接口：**

| 函数 | 说明 |
|------|------|
| `init_disasm(triple)` | 初始化反汇编器，参数为目标架构标识（如 "riscv32"） |
| `disassemble(str, size, pc, code, nbyte)` | 反汇编指令，输出汇编代码到缓冲区 `str` |

**使用示例：**
```c
// 初始化反汇编器（通常在程序启动时调用）
init_disasm("riscv32");

// 反汇编一条指令
char asm_str[100];
uint8_t code[] = {0x13, 0x00, 0x00, 0x00}; // addi x0, x0, 0
disassemble(asm_str, sizeof(asm_str), 0x80000000, code, 4);
// asm_str 会被填充为反汇编字符串
```

### 2. iringbuf.c - 指令环形缓冲区

记录最近执行的若干条指令，在程序出错时输出以方便调试。

**主要接口：**

| 函数 | 说明 |
|------|------|
| `iringbuf_write(logbuf)` | 将一条指令的日志记录到环形缓冲区 |
| `iringbuf_display()` | 打印环形缓冲区的内容（通常在程序异常终止时调用） |

**使用示例：**
```c
// 在执行每条指令后调用
iringbuf_write(_this->logbuf);  // _this 是 Decode 结构体指针

// 程序异常时显示最近执行的指令历史
iringbuf_display();
```

### 3. log.c - 日志系统

提供日志输出功能，支持将日志写入文件或标准输出。

**主要宏和接口：**

| 宏/函数 | 说明 |
|---------|------|
| `log_write(...)` | 按照格式字符串输出日志（类似 printf），受 `log_enable()` 控制 |
| `init_log(log_file)` | 初始化日志系统，指定日志文件路径；若参数为 NULL 则输出到 stdout |
| `log_enable()` | 检查日志记录是否启用（由 `CONFIG_TRACE_START/END` 控制） |

**使用示例：**
```c
// 初始化日志系统
init_log("nemu.log");  // 或 init_log(NULL) 输出到标准输出

// 在程序中输出日志
log_write("PC=0x%x, instruction=%s\n", cpu.pc, asm_str);

// 检查日志是否启用
if (log_enable()) {
  log_write("Debug info: %d\n", value);
}
```

### 4. state.c - 模拟器状态管理

管理 NEMU 模拟器的运行状态。

**全局变量和函数：**

| 符号 | 说明 |
|------|------|
| `nemu_state` | 全局状态结构体，包含当前运行状态、PC 和返回值 |
| `is_exit_status_bad()` | 检查程序是否异常退出（返回 1 表示异常，0 表示正常退出） |

**状态值：**
- `NEMU_RUNNING` - 正在运行中
- `NEMU_STOP` - 暂停（还未运行或被暂停）
- `NEMU_END` - 程序执行结束（正常停止）
- `NEMU_ABORT` - 程序执行异常终止（错误或断言失败）
- `NEMU_QUIT` - 用户请求退出模拟器

**使用示例：**
```c
// 检查程序是否正常退出
if (is_exit_status_bad()) {
  printf("Program exited abnormally\n");
}

// 访问模拟器状态
printf("Final PC: 0x%x, Return value: %d\n", nemu_state.halt_pc, nemu_state.halt_ret);
```

### 5. timer.c - 时间和随机数

提供时间测量和随机数生成功能。

**主要接口：**

| 函数 | 说明 |
|------|------|
| `get_time()` | 获取自程序启动以来的经过时间（单位：微秒）|
| `init_rand()` | 初始化随机数生成器（通常在程序启动时调用） |

**使用示例：**
```c
// 初始化随机数生成器
init_rand();

// 测量代码执行时间
uint64_t start = get_time();
// ... 执行一些操作 ...
uint64_t elapsed = get_time() - start;
printf("Elapsed time: %ld us\n", elapsed);
```

## 宏定义

### ANSI 颜色代码

| 宏 | 含义 |
|----|------|
| `ANSI_FG_RED`, `ANSI_FG_GREEN` 等 | 前景色（文本颜色） |
| `ANSI_BG_RED`, `ANSI_BG_GREEN` 等 | 背景色 |
| `ANSI_FMT(str, fmt)` | 为字符串应用颜色格式，返回带颜色的字符串 |

**使用示例：**
```c
printf("%s\n", ANSI_FMT("Error message", ANSI_FG_RED));  // 输出红色文本
```









