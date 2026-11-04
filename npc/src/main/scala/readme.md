# AzazeRV 核心

顶层为 `core/AzazeRV.scala`。IFU、IDU、EXU、LSU、WBU 通过握手和队列连接，
`pipeline_busy` 控制指令发射；跳转时冲刷取指与译码队列。

- `core/ifu`：PC、取指和旧响应丢弃。
- `core/idu`：指令译码。
- `core/backend`：整数运算、乘除法和 CSR。
- `core/lsu`：字节/半字/字访存和写掩码。
- `core/wbu`：提交与寄存器写回。
- `Arbiter.scala`、`PMEM.scala`：取指与数据访问仲裁、DPI 内存接口。
- `device/RAM.scala`：待接入的硬件 RAM。

构建与运行命令见项目根目录的 `README.md`。
