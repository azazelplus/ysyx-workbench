/***************************************************************************************
 * mtrace.h - 内存访问追踪 (Memory Access Trace) for NPC2
 * 
 * 功能：
 * 1. 记录内存读写操作到环形缓冲区（非REALTIME模式）
 * 2. 实时打印每次内存访问（REALTIME模式）
 * 3. 支持地址范围过滤
 * 
 * 配置（在 main.cpp 中）：
 * - ENABLE_MTRACE: 编译时开关，false 时整个功能不编译
 * - REALTIME_MTRACE: 运行时模式，true=实时打印，false=环形缓冲区
 ***************************************************************************************/

#ifndef __MTRACE_H__
#define __MTRACE_H__

#include <cstdint>
#include <cstdio>
#include <cstring>

// 配置应在 config.h 中定义，并在 include 此文件前先 include config.h
#ifndef ENABLE_MTRACE
#error "Please include config.h before mtrace.h"
#endif

// 运行时条件表达式，默认为始终打印
#ifndef CONFIG_MTRACE_COND_EXPR
#define CONFIG_MTRACE_COND_EXPR true
#endif

#if ENABLE_MTRACE

// 环形缓冲区大小
#define MTRACE_BUF_SIZE 32

// 单条日志缓冲区大小
#define MTRACE_LOG_SIZE 128

// 访问类型
enum MTraceType {
    MTRACE_READ  = 0,
    MTRACE_WRITE = 1
};

// 环形缓冲区条目
struct MTraceEntry {
    uint32_t addr;           // 访问地址
    uint32_t data;           // 数据
    uint32_t pc;             // 当时的 PC
    uint64_t cycle;          // 仿真周期
    uint8_t len;             // 访问长度 (1/2/4 字节)
    MTraceType type;         // 读/写
    bool valid;              // 是否有效
};

// 内存追踪类
class MTrace {
private:
    MTraceEntry entries[MTRACE_BUF_SIZE];
    int head;
    int curr;
    bool is_realtime;      // 是否实时打印

    // 地址过滤配置
    bool range_enabled;
    uint32_t range_start;
    uint32_t range_end;

    // 检查地址是否在追踪范围内
    bool in_range(uint32_t addr);

    // 格式化日志
    void format_log(char *buf, size_t size, const MTraceEntry &e);

public:
    MTrace();

    /**
     * 配置追踪功能
     */
    void set_realtime(bool real) { is_realtime = real; }
    
    /**
     * 配置地址过滤范围
     * @param en: 是否开启过滤
     * @param start: 起始地址 (inclusive)
     * @param end: 结束地址 (exclusive)
     */
    void set_range_filter(bool en, uint32_t start = 0, uint32_t end = 0) {
        range_enabled = en;
        range_start = start;
        range_end = end;
    }

    /**
     * write - 记录一次内存访问
     */
    void write(uint32_t addr, uint32_t data, uint8_t len, MTraceType type, 
               uint32_t pc, uint64_t cycle);

    /**
     * display_ringbuf - 显示环形缓冲区内容
     */
    void display_ringbuf();
};

#else  // !ENABLE_MTRACE

// 访问类型（即使禁用也需要定义，因为 dpic.cpp 使用）
enum MTraceType {
    MTRACE_READ  = 0,
    MTRACE_WRITE = 1
};

// 空实现
class MTrace {
public:
    void set_realtime(bool real) {}
    void set_range_filter(bool en, uint32_t start = 0, uint32_t end = 0) {}
    void write(uint32_t addr, uint32_t data, uint8_t len, MTraceType type, 
               uint32_t pc, uint64_t cycle) {}
    void display_ringbuf() {}
};

#endif  // ENABLE_MTRACE

// 全局实例
extern MTrace mtrace;

#endif /* __MTRACE_H__ */