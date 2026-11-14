#ifndef NPC_DEVICE_H
#define NPC_DEVICE_H

#include <cstdint>

// 只处理设备寄存器；普通内存读写由 memory 模块负责。
extern "C" uint32_t mmio_read(uint32_t raddr);
extern "C" void mmio_write(uint32_t waddr, uint32_t wdata, uint8_t wmask);

// ============ 外设初始化接口 ============

/**
 * init_device - 初始化外设（串口、时钟等）
 * 
 * 【调用时机】在仿真开始前调用，记录系统启动时间
 */
void init_device();

#endif // NPC_DEVICE_H
