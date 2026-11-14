# NPC — AzazeRV

Chisel 实现的 RV32 CPU，使用多周期握手与队列连接 IFU、IDU、EXU、LSU、WBU。
核心默认支持 RV32I/M，硬件复位 PC 为 `0x80000000`。

## 构建

在工作区根目录执行：

```sh
make -C abstract-machine/am ARCH=riscv32-npc sim
make -C am-kernels/tests/cpu-tests ARCH=riscv32-npc ALL=dummy run
```

AM 的 `scripts/platform/npc.mk` 定义内存与设备地址，并通过 `-D` 传给驱动和仿真器。
C++ 仿真器由 `sim/sim.mk` 使用 Verilator 构建，普通内存由 `src/main/scala/device/RAM.scala` 实现。
`sim/src/memory/memory.cpp` 在启动时把 `.bin` 装入硬件 RAM，并为调试器提供同一份内存的读取接口。

单独生成硬件：

```sh
cd npc
sbt 'runMain azazerv.AzazeRV'
```

源码测试：`sbt test`。Mill 模块名为 `npc`，入口为 `azazerv.AzazeRV`。

`src/test` 中的测试按需运行，不参与 `make run` 的硬件生成依赖。
例如修改 LSU 后，在 `npc` 目录执行 `sbt 'testOnly azazerv.LSUHandshakeSpec'`。

| 测试 | 检查的具体行为 | 对应的后续开发 |
| --- | --- | --- |
| `ExecutionSpec` | 快速/迭代乘法、除零与有符号边界、下游暂停时的跳转和 CSR 副作用 | 修改 MDU、EXU 和流水线控制 |
| `MemoryAccessSpec` | LB/LH/LW/LBU/LHU 的扩展、SB/SH/SW 的掩码，以及控制信号经过级间寄存器后是否保持 | 修改 IDU 访存控制和流水线寄存器 |
| `LSUHandshakeSpec` | 延迟响应时只发一次请求、AW/W 分开握手、写回反压时保持结果 | 接入 Cache 或改变存储器延迟 |
| `XbarSpec` | 地址范围端点、未映射地址、响应隔离及组合转发时序 | 修改地址布局、增加设备和路由优化 |
| `ArbiterXbarSpec` | IFU/LSU 争用、响应归属，以及上一笔结束前阻塞下一笔请求 | 修改仲裁策略和事务管理 |
| `AXI4BurstSpec` | 当前单笔在途链路的多拍读写、最后一拍释放路由、ID 透传和错误响应拍数 | 接入 Cache 的突发填充与写回 |
| `MMIOSpec` | 用计数设备模型验证每笔事务只访问设备一次，反压时保持响应 | 修改 MMIO 握手和接入有读取副作用的设备 |

- [平台地址与内存流程](docs/platform.md)
- [核心后端说明](src/main/scala/core/backend/README.md)
- [调试笔记](readme/readme.md)

当前普通内存路径为 IFU / LSU → Arbiter → Xbar → RAM；UART / RTC 使用独立的 MMIO 从机。
