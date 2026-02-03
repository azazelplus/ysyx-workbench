/***************************************************************************************
 * itrace.h - 指令执行追踪 (Instruction Trace) for NPC2
 * 
 * 功能：
 * 1. 实时打印每条指令执行信息（REALTIME模式）
 * 2. 记录最近执行的 N 条指令到环形缓冲区（非REALTIME模式）
 * 3. 程序崩溃或超时时显示环形缓冲区的指令历史，方便调试
 * 
 * 配置（在 main.cpp 中）：
 * - ENABLE_ITRACE: 编译时开关，false 时整个功能不编译
 * - REALTIME_ITRACE: 运行时模式，true=实时打印，false=环形缓冲区
 ***************************************************************************************/

#ifndef __ITRACE_H__
#define __ITRACE_H__

#include <cstdint>
#include <cstdio>
#include <cstring>

// 配置应在 config.h 中定义，并在 include 此文件前先 include config.h
#ifndef ENABLE_ITRACE
#error "Please include config.h before itrace.h"
#endif

// 运行时条件表达式，默认为始终打印
#ifndef CONFIG_ITRACE_COND_EXPR
#define CONFIG_ITRACE_COND_EXPR true
#endif

#if ENABLE_ITRACE

#include <string>

// 环形缓冲区大小（指令条数）
#define ITRACE_BUF_SIZE 16

// 单条指令的日志缓冲区大小
#define INST_LOG_SIZE 128

// 环形缓冲区条目
struct ITraceEntry {
    uint32_t pc;                         // 指令地址
    uint32_t inst;                       // 指令机器码
    uint64_t cycle;                      // 仿真周期
    bool valid;                          // 该条目是否有效
};

// 指令追踪类
class ITrace {
private:
    ITraceEntry entries[ITRACE_BUF_SIZE];  // 环形缓冲区
    int head;                              // 下一个写入位置
    int curr;                              // 最后写入的位置（用于标记出错指令）
    bool is_realtime;                      // 是否实时打印

public:
    ITrace();

    /**
     * 配置追踪功能
     */
    void set_realtime(bool real) { is_realtime = real; }

    /**
     * write - 记录一条指令执行信息
     * @pc: 指令地址
     * @inst: 指令机器码
     * @cycle: 仿真周期（用于打印）
     */
    void write(uint32_t pc, uint32_t inst, uint64_t cycle = 0);

    /**
     * display_ringbuf - 显示环形缓冲区的内容
     * 
     * 用 "-->" 标记最后执行的指令（通常是出错的指令）。
     * 这个函数通常在 BAD TRAP 或超时时调用。
     */
    void display_ringbuf();
};

#else  // !ENABLE_ITRACE

// 空实现：当 ENABLE_ITRACE = false 时，编译空壳类以避免链接错误
class ITrace {
public:
    void set_realtime(bool real) {}
    void write(uint32_t pc, uint32_t inst, uint64_t cycle = 0) {}
    void display_ringbuf() {}
};

#endif  // ENABLE_ITRACE

// 全局实例
extern ITrace itrace;

#endif /* __ITRACE_H__ */