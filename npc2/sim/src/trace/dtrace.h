/***************************************************************************************
 * dtrace.h - 设备访问追踪 (Device Trace) for NPC2
 *
 * 功能：
 * 1. 记录 MMIO/内存访问的读写日志，便于调试外设行为
 * 2. 支持运行时条件表达式 CONFIG_DTRACE_COND_EXPR 控制输出
 ***************************************************************************************/

#ifndef __DTRACE_H__
#define __DTRACE_H__

#include <cstdint>
#include <cstdio>

// 配置应在 config.h 中定义，并在 include 此文件前先 include config.h
#ifndef ENABLE_DTRACE
#error "Please include config.h before dtrace.h"
#endif

// 运行时条件表达式，默认为始终打印
#ifndef CONFIG_DTRACE_COND_EXPR
#define CONFIG_DTRACE_COND_EXPR true
#endif

#if ENABLE_DTRACE

class DTrace {
public:
    // 记录一次读操作
    void log_read(const char* dev, uint32_t addr, uint32_t data, int len);

    // 记录一次写操作
    void log_write(const char* dev, uint32_t addr, uint32_t data, int len);
};

#else  // !ENABLE_DTRACE

class DTrace {
public:
    void log_read(const char*, uint32_t, uint32_t, int) {}
    void log_write(const char*, uint32_t, uint32_t, int) {}
};

#endif // ENABLE_DTRACE

// 全局实例
extern DTrace dtrace;

#endif /* __DTRACE_H__ */