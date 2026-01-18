
# MiniRV - 简易 RISC-V CPU

一个使用 Chisel + Mill 框架实现的简易 RV32I CPU。

## 模块结构

```
MiniRV (顶层)
├── IFU (取指单元) - 维护 PC，从指令存储器读取指令
├── IDU (译码单元) - 解析指令，生成立即数，读寄存器，生成控制信号
├── EXU (执行单元) - ALU 运算，分支/跳转计算
├── LSU (访存单元) - 数据存储器读写
├── WBU (写回单元) - 将结果写回寄存器堆
└── RegFile (寄存器堆) - 32 个通用寄存器
```

## 当前状态

- [x] 基础框架搭建
- [ ] 完整 RV32I 指令支持
- [ ] 分支指令完善 (目前仅 BEQ)
- [ ] Load/Store 字节/半字支持
- [ ] 测试用例

## 构建命令

```bash
# 编译
./mill chisel_template.compile

# 生成 Verilog
./mill chisel_template.runMain minirv.MiniRV

# 运行测试
./mill chisel_template.test
```


