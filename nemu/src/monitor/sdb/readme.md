
src/monitor/sdb/ 下是 NEMU 的简单调试器 SDB (Simple Debugger) 的代码

## 主要功能

SDB 是一个类似 GDB 的交互式调试器，提供单步执行、内存检查、表达式求值和监视点等功能。

### 核心命令

| 命令 | 缩写 | 说明 | 示例 |
|------|------|------|------|
| `continue` | `c` | 继续执行程序 | `c` |
| `quit` | `q` | 退出调试器 | `q` |
| `stepi` | `si` | 单步执行指定数量的指令 | `si 10` |
| `help` | `h` | 显示帮助信息 | `h` |
| `info` | `i` | 显示寄存器或监视点信息 | `i r`/`i w` |
| `examine` | `x` | 检查内存中的内容 | `x 10 0x80000000` |
| `print` | `p` | 表达式求值 | `p $eax + 0x10` |
| `watch` | `w` | 设置监视点 | `w $eax` |
| `delete` | `d` | 删除监视点 | `d 1` |

### 核心模块

- **sdb.c/sdb.h** - 调试器主逻辑，包括命令处理和 REPL 循环
- **expr.c** - 表达式求值器，支持 C 语言风格的表达式解析和计算
- **watchpoint.c** - 监视点管理，用于追踪内存/寄存器变化

### 关键函数接口

#### 命令处理函数（内部使用）

```c
// 核心命令处理（在 sdb.c 中实现）
static int cmd_c(char *args);       // continue
static int cmd_q(char *args);       // quit
static int cmd_si(char *args);      // stepi
static int cmd_help(char *args);    // help
static int cmd_info(char *args);    // info
static int cmd_x(char *args);       // examine
static int cmd_p(char *args);       // print
static int cmd_w(char *args);       // watch
static int cmd_d(char *args);       // delete

// 调试器初始化和主循环
void init_sdb();                    // 初始化 SDB（正则表达式、监视点池等）
void sdb_mainloop();                // 启动交互式调试循环
```

#### 表达式求值（expr.c）

```c
// 表达式求值接口
word_t expr(char *e, bool *success);

// 说明：
// - 参数 e 是一个表达式字符串，可包含 C 语言风格的运算符和操作数
// - 参数 success 用于输出求值是否成功
// - 返回值是计算结果
// - 支持的操作符：+ - * / % & | ^ << >> == != < > <= >= && ||
// - 支持的操作数：10进制/16进制数字、寄存器（$reg）
```

#### 监视点管理（watchpoint.c）

```c
// 监视点管理接口
void init_wp_pool();                         // 初始化监视点池
void display_wp();                           // 显示所有监视点及其当前值
WatchPoint* create_wp(const char *expr_str); // 创建监视点，返回监视点指针
bool delete_wp(int no);                      // 删除指定编号的监视点
bool scan_watchpoints();                     // 扫描所有监视点，检测值变化
                                             // 返回 true 表示有监视点被触发

// 说明：
// - 监视点用于追踪表达式值的变化
// - 如果被监视的表达式值改变，程序会暂停并提示用户
// - 监视点编号从 1 开始自动分配
```

### 使用示例

#### 基础调试流程

```bash
# 在 NEMU 启动后进入 SDB 交互模式
(nemu) si 5              # 单步执行 5 条指令
(nemu) x 10 0x80000000   # 显示从 0x80000000 开始的 10 个字
(nemu) p $sp + 4         # 计算 $sp + 4 的值
(nemu) i r               # 显示所有寄存器
(nemu) c                 # 继续执行
(nemu) q                 # 退出 NEMU
```

#### 使用监视点追踪变量变化

```bash
(nemu) w $sp             # 设置监视点1：监视栈指针
(nemu) w $a0 > 0x1000    # 设置监视点2：监视参数是否大于 0x1000
(nemu) i w               # 显示所有监视点及其值
(nemu) c                 # 继续执行，若监视点值改变则停止
(nemu) d 1               # 删除监视点1
```

#### 表达式求值示例

```bash
(nemu) p $a0             # 打印寄存器 a0 的值
(nemu) p $a0 + $a1 * 2   # 计算表达式
(nemu) p (*(unsigned int *) 0x80000000)  # 读取内存（注：需要表达式求值器支持）
```

### 内存检查命令详解

**`examine` (缩写 `x`) 命令格式：`x [数量] [地址]`**

```bash
(nemu) x 10 0x80000000   # 显示 10 个字（word）
(nemu) x 4 $sp           # 显示 4 个字，从 $sp 开始
```

- 默认单位是字（通常为 4 字节）
- 地址可以是数字或表达式
- 用于检查内存内容、栈信息等

### 监视点原理

1. **创建阶段**：用户指定一个表达式（如 `$a0`、`$sp + 0x10` 等）
2. **初始化**：记录表达式的初始值
3. **检测阶段**：每次指令执行后，重新计算表达式的值
4. **触发条件**：如果值与上一次不同，设置标志，程序下次进入调试器时停止
5. **显示**：用户进入调试器后可以用 `i w` 查看监视点的旧值和新值

这种机制对于追踪难以发现的 bug 非常有用，例如：
- 追踪栈指针的意外变化
- 监控函数参数的修改
- 检测特定地址的内存变化

### 调试技巧

#### 定位 Bug 的最后时刻

```bash
(nemu) w $pc             # 监视程序计数器的跳跃
(nemu) c                 # 继续运行，当 $pc 发生异常跳转时停止
```

#### 追踪函数调用栈

```bash
(nemu) si                # 单步进入函数
(nemu) i r               # 查看 $ra（返回地址）和 $sp（栈指针）
```

#### 检查内存布局

```bash
(nemu) x 32 0x80000000   # 显示程序起始地址后的内存内容
(nemu) x 8 $sp           # 显示栈上的数据
```







