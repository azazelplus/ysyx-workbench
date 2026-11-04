#ifndef AM_NPC_PLATFORM_H
#define AM_NPC_PLATFORM_H

// NPC 的设备地址来自平台配置，RAM 终点由同一配置生成链接符号。
extern char _pmem_end;
#define PMEM_END ((uintptr_t)&_pmem_end)
#define SERIAL_PORT NPC_UART_BASE
#define RTC_ADDR NPC_RTC_BASE
#endif
