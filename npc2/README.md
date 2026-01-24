# 1. 简介 NPC2 - MiniRV 单周期处理器

这是 `npc` 项目的简化版本，将五级流水线 CPU 改为**单周期处理器**。

## 与 npc 的区别

| 特性 | npc (五级流水线) | npc2 (单周期) |
|------|------------------|---------------|
| 流水线寄存器 | IF/ID, ID/EX, EX/MEM, MEM/WB | 无 |
| 数据前递 (FWU) | 有 | 无（不需要） |
| 冒险检测 (HDU) | 有 | 无（不需要） |
| 暂停/刷新逻辑 | 有 | 无 |
| 每条指令周期数 | 1 (理想情况) | 1 |
| 复杂度 | 高 | 低 |

## 已移除的模块

- `HDU/hdu.scala` - 冒险检测单元
- `FWU/fwu.scala` - 数据前递单元

## 保留的模块

- `IFU/` - 取指单元
- `IDU/` - 译码单元
- `EXU/` - 执行单元
- `LSU/` - 访存单元
- `WBU/` - 写回单元
- `RegFile.scala` - 寄存器堆
- `PMEM.scala` - 物理存储器接口
- `DPIC.scala` - DPI-C 黑盒模块
- `Common.scala` - 共享定义

## 使用方法

### 编译项目

```bash
cd npc2
./mill npc2.compile
```

### 生成 Verilog

```bash
./mill npc2.runMain minirv.MiniRV
```

生成的文件在 `generated/` 目录下。

### 运行仿真

```bash
./sim/sim.sh <program.bin> [max_cycles]
```

例如：
```bash
./sim/sim.sh ../am-kernels/tests/cpu-tests/build/dummy-riscv32-npc.bin 10000
```

## 单周期 CPU 工作原理

在单周期 CPU 中，每条指令在一个时钟周期内完成：

```
         ┌─────────────────────────────────────────────────────────┐
         │                      单个时钟周期                          │
         ├─────┬─────┬─────┬─────┬─────┐                            │
         │ IF  │ ID  │ EX  │ MEM │ WB  │                            │
         └─────┴─────┴─────┴─────┴─────┘                            │
                                                                     │
PC(n) ──►取指 ──► 译码 ──► 执行 ──► 访存 ──► 写回 ──► PC(n+1)         │
         └─────────────────────────────────────────────────────────┘
```

**优点：**
- 结构简单，无数据冒险和控制冒险问题
- 调试容易

**缺点：**
- 时钟频率受最慢阶段限制（通常是访存）
- 硬件利用率低

## 后续改进建议

1. 添加更多测试用例验证功能正确性
2. 添加性能计数器（周期数、指令数等）
3. 支持更多 RISC-V 指令（如 M 扩展的乘除法）
4. 优化关键路径以提高时钟频率

# 2. debug log

## 2.1

修改itrace.h的itrace实现, 结果在cpu-tests/下的`make ARCH=riscv32-npc2 ALL=dummy run` 始终没变化.

问题: `sim.mk`的依赖没有包括头文件, 加上我很不合乎常理地把itrace的实现逻辑都放在了itrace.h, 而不是itrace.cpp.



## 2.2 ELF文件

处理elf文件.

现代程序elf的program headers的physaddr没意义.


![alt text](image.png)

![alt text](image-2.png)

elf文件大致的layout:
* elf header
* program headers table
* segments(used by the)
* sections
* section

* 每个`section`(节) 的节头`section head`在哪里? 都以`数组`的方式记录在`elf head`中. `elf head`在elf的起始位置.
* C规定所有未初始化的全局变量, 都初始化成一套固定的形式(0或者空指针(结构体)或者null(指针)). 所以它们被存放在elf的`.bss`节. 因为这并不是需要记录的数据, 所以`.bss`节的大小是0.
* 字符串字面量存储在.rodata节. 因为C规定字符串是只读的.
* `strtab`节的存在: 解决这样一个问题: 有的节的名字太长了. 于是它们只记录指针, 指向`strtab`节中存储的节名称字符串.


## 2.3 链接: 符号解析+重定位


编译以文件为单位进行.

![alt text](image-4.png)   


![alt text](image-5.png)


## 2.4 符号解析

为了让链接器ld解析符号, 汇编器as在生成目标文件时, 会把`符号表`放在`elf`目标文件中.

即`.symtab`节. 也叫符号表(symbol table).  查看: `readelf -s file.elf`

其中:
* Name: 符号的名称.
* Ndx: **符号所在的节的索引**. 特殊值:
  * **UND: 未定义符号.** 该符号在本目标文件中没有定义, 需要从其他目标文件中引用. 如果某个文件调用了另一个文件的函数`my_lib()`, 则本文件编译出的elf中会有一个`my_lib`符号, 其Ndx就是`UND`.
  * ABS: 绝对符号. 该符号的值是一个绝对地址, 不会随着节的加载地址变化而变化.
  * COM: 公共符号. 该符号在多个目标文件中可能有定义, 链接器会合并它们.
* Num: 符号编号. 从1开始编号. 0号符号保留, 表示无效符号.
* Value: 符号的值. 对于变量, 通常是变量的地址; 对于函数, 通常是函数的入口地址.
* Size: 符号的大小. 对于变量, 是变量的字节数; 对于函数, 是函数的代码字节数.
* Type: 符号的类型. 常见类型有:
  * NOTYPE: 无类型
  * OBJECT: 变量
  * FUNC: 函数
* Bind: 符号的绑定属性. 常见绑定有:
  * LOCAL: 局部符号, 只能在本目标文件中使用.
  * GLOBAL: 全局符号, 可以在其他目标文件中引用.
  * WEAK: 弱符号, 如果有多个定义, 链接器会选择一个.
* Vis: 符号的可见性. 通常是 DEFAULT




![alt text](image-6.png)


链接器的工作大致原理: 维护两个集合D和U, 扫描所有参与链接的目标文件的符号表:

* 一开始两个集合都是空集...

![alt text](image-8.png)

* 注意: 上图所说的`全局符号`指的是在符号表中`Bind`字段为`GLOBAL`的符号, 不是C程序的全局变量. 
* ld只对`GLOBAL`符号做遍历处理, 忽略其他符号, 不会把它们拿去和别的文件的 `U`集合做匹配。
* C中的static 全局变量, 会在符号表生成一个`Bind`=`LOCAL`的符号. ld不会处理它们.




![alt text](image-9.png)


![alt text](image-10.png)


由于mangling的存在, C++程序中的函数名和编译得到的elf的符号表中的符号名称不一定相同.
`g()`的符号会变成`_Z1gv`, `_Z1gi`等.
![alt text](image-11.png)


### 3. 

`extren`不会检查变量类型. 如果本来其他地方定义的全局变量`a`,`b`是int, 你用float或者double的方式extern到别的文件用, 就会出现未定义的行为.(这些数据结构的内存布局不一样, 访存的时候会出问题).

链接器不看这些, 只要符号名字一样就链接成功.

![alt text](image-12.png)

### 4. C语言的试探性定义

变量不赋初值称为试探性定义/暂定定义(tentative definition), 在这之后允许你再定义一次.

- tentative defination的变量对应一个`weak symbol`
- 正式定义的变量对应一个`strong symbol`

在`-fno-common`选项下:

ld看到强符号, 直接加入D集合. 

看到试探性定义符号, 放入`COM`节. 它可以看成一种特殊的`UND`.

* 如果遍历结束后, D集合中有同名强符号, 将`COM`节中的符号解析到它;
* 如果遍历结束后, D集合也没有找到同名强符号, 则将所有现存的`COM`同名符号都解析到它们中最大的那个.


```C
int a;  // tentative definition
int a = 5;  // 正式定义
```

下面的例子中,
![alt text](image-14.png)




![alt text](image-13.png)






![alt text](image-15.png)


## 2.5 重定位

链接脚本将所有参与的目标文件合并. 对应节也要合并.

要做的事: 
* 内存布局:
  * 确定各节在内存中的地址;
  * `.`代表当前地址
* 符号定义:
  * 定义一些符号给程序使用
* 入口地址
  * ENTRY(_start)


am的链接脚本在`/home/azazel/ysyx-workbench/abstract-machine/scripts/linker.ld`.


![alt text](image-16.png)

### 6. 链接器松弛, code module

![alt text](image-17.png)

![alt text](image-18.png)

下面的`large.buf.c`必须使用`-mcmodel=large`选项编译链接, 实现更远的寻址. 否则会报错.
 
![alt text](image-19.png)


## 2.6 ftrace和尾调用优化


运行cpu-tests中的recursion为例, ftrace的部分示例输出如下 (仅供参考, 不同的编译器版本可能会得到不同的输出, 你可以结合反汇编结果进行理解):

```
0x8000000c: call [_trm_init@0x80000260]
0x80000270:   call [main@0x800001d4]
0x800001f8:     call [f0@0x80000010]
0x8000016c:       call [f2@0x800000a4]
0x800000e8:         call [f1@0x8000005c]
0x8000016c:           call [f2@0x800000a4]
0x800000e8:             call [f1@0x8000005c]
0x8000016c:               call [f2@0x800000a4]
0x800000e8:                 call [f1@0x8000005c]
0x8000016c:                   call [f2@0x800000a4]
0x800000e8:                     call [f1@0x8000005c]
0x8000016c:                       call [f2@0x800000a4]
0x800000e8:                         call [f1@0x8000005c]
0x80000058:                         ret  [f0]              # 注释(2)
0x800000fc:                       ret  [f2]                # 注释(1)
0x80000180:                       call [f2@0x800000a4]
0x800000e8:                         call [f1@0x8000005c]
0x80000058:                         ret  [f0]
0x800000fc:                       ret  [f2]
0x800001b0:                     ret  [f3]                  # 注释(3)
0x800000fc:                   ret  [f2]
0x80000180:                   call [f2@0x800000a4]
0x800000e8:                     call [f1@0x8000005c]
0x8000016c:                       call [f2@0x800000a4]
0x800000e8:                         call [f1@0x8000005c]
0x80000058:                         ret  [f0]
```

如果你仔细观察上文`recursion`的示例输出, 你会发现一些有趣的现象. 具体地, 注释(1)处的`ret`的函数是和对应的`call`匹配的, 也就是说, `call`调用了`f2`, 而与之对应的`ret`也是从`f2`返回; 但注释(2)所指示的一组`call`和`ret`的情况却有所不同, `call`调用了`f1`, 但却从`f0`返回; 注释(3)所指示的一组`call`和`ret`也出现了类似的现象, `call`调用了`f1`, 但却从`f3`返回.

尝试结合反汇编结果, 分析为什么会出现这一现象?



答案就是编译器优化.



想象一下这个场景：

-   **f0** 函数的结尾是：`恢复寄存器; ret;`（地址在 `0x80000058`）
    
-   **f1** 函数的逻辑跑完了，发现它最后也要做：`恢复寄存器; ret;`
    

编译器为了节省空间，心想：“既然大家最后都要做同样的事，`f1` 干脆别自己写 `ret` 了，直接一脚\*\*跳转（Jump）\*\*到 `f0` 的 `ret` 那里去吧！”

**结果就是：**

1.  CPU 执行了 `call f1`：`ftrace` 记下 `call [f1]`，深度 **+1**。
    
2.  `f1` 执行到最后，一个 `j 0x80000058` 跳到了 `f0` 的领地。
    
3.  CPU 在 `0x80000058` 执行了 `ret`：`ftrace` 查表发现这个地址属于 `f0`，于是记下 `ret [f0]`，深度 **\-1**。
    

**结论：** 这种情况下的**数量是匹配的**（一增一减），所以**缩进不会乱**。只是由于 `ftrace` 比较“笨”，它只看地址不看逻辑，所以报出的名字让你产生了“灵异事件”的错觉。

**后果：** 如果这种情况多发生几次，你的 `call_depth = call数量-ret数量` 可能会变成**负数**，或者所有的缩进都会往左边“缩进”到屏幕外面去。这就是为什么在调试经过高度优化的代码（如 `-O2`, `-O3`）时，`ftrace` 的输出往往看起来像一团乱麻。











