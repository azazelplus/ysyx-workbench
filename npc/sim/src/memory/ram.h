#ifndef NPC_RAM_H
#define NPC_RAM_H

class VAzazeRV;

// 把 RAM.scala 的存储阵列绑定给 memory.cpp；生成模型的层级名称集中在 ram.cpp。
bool ram_bind(VAzazeRV *dut);

#endif
