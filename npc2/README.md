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


## 2.3 静态链接


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

变量不赋初值称为试探性定义/暂定定义(tentative definition), 在这之后允许你再定义一次:
```C
int a;  // tentative definition
int a = 5;  // 正式定义
```
![alt text](image-14.png)
![alt text](image-13.png)
![alt text](image-15.png)
