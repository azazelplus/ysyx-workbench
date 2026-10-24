# ALU / MDU 接入说明

`IDU -> ID2EX -> EXU` 共用 `azazerv.ALUFunction`，字段名为 `alu_func`。
IDU 负责选择两个操作数；EXU 将数值和运算类型交给 ALU/MDU。

| 指令 | alu_src1 | alu_src2 | 运算 / 写回 |
| --- | --- | --- | --- |
| R 型整数 / M 扩展 | rs1 | rs2 | 指定运算 |
| I 型整数 | rs1 | imm | 指定运算 |
| load / store | rs1 | imm | add，得到访存地址 |
| LUI | zero | imm | add |
| AUIPC | pc | imm | add |
| JAL / JALR | pc | four | add，得到链接地址 |

EXU 单独计算跳转目标、判断分支条件、选择 CSR 读回值；ALU 不识别指令。
旧的 `ALUControl` 注释草稿已经删除，译码集中在 IDU。

## MDU 握手

M 指令停留在上游 `id_ex_q`，直到 EXU 输出握手才弹出。
`mdu_started` 防止等待期间重复发送请求；MDU 完成后保持响应，直到下游接收。
普通指令仍可组合直通。CSR 写入、异常信号和跳转重定向在 `io.out.fire` 时触发。

## 配置

生成入口 `object AzazeRV` 创建唯一的 `implicit val cfg: CoreConfig = CoreConfig()`，
顶层、各流水级和共享 Bundle 通过隐式构造参数接收同一份配置。

- `entryAddr`：PC 复位地址，默认 `0x80000000L`。
- `hasRVM`：是否译码 M 指令并实例化 MDU，默认 `true`。
- `fastMul`：快乘或迭代乘法，默认 `true`，与是否支持 M 扩展分别控制。
- 当前核心、CSR、LSU 和 DPI 接口仍要求 32 位地址/数据、32 个 GPR 和 12 位 CSR 地址，配置构造时检查。
- 尚无通用非法指令异常路径；`hasRVM=false` 时遇到 M 指令会触发仿真断言，并禁止寄存器写入。
- 预测器、指令 ID、性能计数使能等原有配置字段仍是预留项，本次未实现这些功能。

## 验证（2026-10-21）

在 `npc2` 目录运行 `sbt test`。`ExecutionSpec` 覆盖：

- IDU/EXU 整数运算、U 型、链接地址、访存地址、CSR、分支和冲刷后的无效指令。
- MDU 快乘、迭代乘法，以及关闭 MDU 的 RV32I 路径。
- 每种乘法配置各 640 组 M 运算，包含符号边界、除零、溢出和固定种子的随机操作数。
- 下游反压时保持结果、仅握手时消费指令与触发副作用。
- 自定义 PC 复位地址及跳转优先级。

已重新生成 RTL 并编译 Verilator 仿真器；`dummy`、`load-store`、`mul-longlong`、`div` 均 GOOD TRAP。
另外用明确包含 640 条 M 指令、逐条比较预期结果的 RV32IM 程序完成整核仿真（22483 周期 GOOD TRAP）。
当前 `ENABLE_DIFFTEST=false`，这些结果不代表已通过差分测试。
