# NPC — AzazeRV

Chisel 实现的 RV32 CPU，使用多周期握手与队列连接 IFU、IDU、EXU、LSU、WBU。
核心默认支持 RV32I/M，硬件复位 PC 为 `0x80000000`。

## 构建

在工作区根目录执行：

```sh
make -C abstract-machine/am ARCH=riscv32-npc sim
make -C abstract-machine/am ARCH=riscv32-npc check-memory
make -C am-kernels/tests/cpu-tests ARCH=riscv32-npc ALL=dummy run
```

AM 的 `scripts/platform/npc.mk` 定义内存与设备地址，并通过 `-D` 传给驱动和仿真器。
C++ 仿真器由 `sim/sim.mk` 使用 Verilator 构建，普通内存由 `sim/src/memory.cpp` 实现。

单独生成硬件：

```sh
cd npc
sbt 'runMain azazerv.AzazeRV'
```

源码测试：`sbt test`。Mill 模块名为 `npc`，入口为 `azazerv.AzazeRV`。

- [平台地址与内存流程](docs/platform.md)
- [核心后端说明](src/main/scala/core/backend/README.md)
- [调试笔记](readme/readme.md)

当前 RAM 接入工作将在 `src/main/scala/device/RAM.scala`、总线适配和镜像装载处继续。
