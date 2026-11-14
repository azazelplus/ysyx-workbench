/***************************************************************************************
 * device.cpp - DPI-C MMIO 设备后端
 * UART / RTC 的寄存器行为和设备追踪在这里实现，不访问普通内存。
 ***************************************************************************************/
#include "config.h"
#include "device.h"
#include "trace/dtrace.h"
#include <cstdio>
#include <ctime>       // 用于时钟功能

// 系统启动时间（用于计算 uptime）
static uint64_t g_boot_time = 0;


/**
 * get_time_us() - 利用库函数获取当前系统时间（微秒）.
 * 
 * @return 当前时间戳，单位为微秒 (us)
 ***************************************************************************************/
static uint64_t get_time_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}


/** 
 * init_device() - 初始化外设
 * 
 * 在仿真开始时调用，记录启动时间
 */
void init_device() {
    g_boot_time = get_time_us();
}


/***************************************************************************************
 * mmio_read - 从 MMIO 设备读取 32 位数据
 * 
 * @param raddr: 读取的物理地址（会自动对齐到 4 字节边界）
 * @return 读取到的 32 位数据；对于无效地址返回 0
 * 
 * MMIO 设备:
 * - 1.NPC_RTC_BASE: 返回系统启动后经过的时间
 *   - 偏移 0: 低 32 位 (us)
 *   - 偏移 4: 高 32 位 (us)
 * - 2. SERIAL
 ***************************************************************************************/
extern "C" uint32_t mmio_read(uint32_t raddr) {
    uint32_t addr = raddr & ~0x3u;  // 对齐到 4 字节边界

    // -------- MMIO: 时钟 (RTC) --------
    // 返回系统启动后经过的微秒数
    if (addr == NPC_RTC_BASE) {
        uint64_t uptime = get_time_us() - g_boot_time;
        uint32_t data = (uint32_t)(uptime & 0xFFFFFFFF);  // 低 32 位
        dtrace.log_read("RTC", addr, data, 4);
        return data;
    }
    if (addr == NPC_RTC_BASE + 4) {
        uint64_t uptime = get_time_us() - g_boot_time;
        uint32_t data = (uint32_t)(uptime >> 32);         // 高 32 位
        dtrace.log_read("RTC", addr, data, 4);
        return data;
    }
    
    return 0;
}

/***************************************************************************************
 * mmio_write - 向 MMIO 设备写入数据
 * 
 * @param waddr: 写入的物理地址（会自动对齐到 4 字节边界）
 * @param wdata: 要写入的 32 位数据
 * @param wmask: 字节写掩码，每个 bit 对应 wdata 中的一个字节
 *               例如 wmask=0x3 表示只写入最低 2 字节
 * 【MMIO 设备】
 * - NPC_UART_BASE：串口输出，写入最低字节到终端
 ***************************************************************************************/
extern "C" void mmio_write(uint32_t waddr, uint32_t wdata, uint8_t wmask) {
    uint32_t addr = waddr & ~0x3u;  // 对齐到 4 字节边界
    uint32_t data = wdata;
    uint8_t mask = wmask & 0x0F;

    // -------- MMIO: 串口输出 --------
    // 将最低字节输出到终端
    if (addr == NPC_UART_BASE) {
        if (mask & 1) {
            uint8_t ch = data & 0xFF;
            dtrace.log_write("SERIAL", addr, ch, 1);
            putchar(ch);
            fflush(stdout); // 确保字符立即输出
        }
        return;
    }

    printf("[WARN] mmio_write: invalid address 0x%08x (ignored)\n", addr);
}
