/***************************************************************************************
 * dtrace.cpp - 设备访问追踪 (Device Trace) 实现 for NPC2
 ***************************************************************************************/

#include "config.h"
#include "trace/dtrace.h"

// 启用/禁用条件表达式
#if ENABLE_DTRACE

#define DTRACE_COND (CONFIG_DTRACE_COND_EXPR)

// 全局实例定义
DTrace dtrace;

void DTrace::log_read(const char* dev, uint32_t addr, uint32_t data, int len) {
    if (DTRACE_COND) {
        printf("[DTRACE] READ  %s: addr=0x%08x, len=%d, data=0x%08x\n",
               dev, addr, len, data);
    }
}

void DTrace::log_write(const char* dev, uint32_t addr, uint32_t data, int len) {
    if (DTRACE_COND) {
        printf("[DTRACE] WRITE %s: addr=0x%08x, len=%d, data=0x%08x\n",
               dev, addr, len, data);
    }
}

#else  // !ENABLE_DTRACE

// 关闭时也要提供全局实例，避免链接错误
DTrace dtrace;

#endif // ENABLE_DTRACE
