/***************************************************************************************
 * itrace.h - 指令执行追踪 (Instruction Trace) for NPC2
 * 
 * 功能：
 * 1. 实时打印每条指令执行信息（模式可选）
 * 2. 记录最近执行的 N 条指令到环形缓冲区
 * 3. 程序崩溃或超时时显示环形缓冲区的指令历史，方便调试
 ***************************************************************************************/

#ifndef __ITRACE_H__
#define __ITRACE_H__

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// ============ 指令追踪模式配置 ============
// ITRACE_REALTIME = 1: 实时打印每条指令
// ITRACE_REALTIME = 0: 只保存到环形缓冲区. 错误/超时时才显示.
#define ITRACE_REALTIME 1

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

public:
    ITrace() : head(0), curr(-1) {
        for (int i = 0; i < ITRACE_BUF_SIZE; i++) {
            entries[i].valid = false;
        }
    }

    /**
     * write - 记录一条指令执行信息
     * @pc: 指令地址
     * @inst: 指令机器码
     * @cycle: 仿真周期（用于打印）
     */
    void write(uint32_t pc, uint32_t inst, uint64_t cycle = 0);

    /**
     * display_ringbuf - 显示环形缓冲区的内容（仅在 ITRACE_REALTIME = 0 时有意义）
     * 
     * 用 "-->" 标记最后执行的指令（通常是出错的指令）。
     * 这个函数通常在 BAD TRAP 或超时时调用。
     */
    void display_ringbuf();
};

// 全局实例
extern ITrace itrace;

#endif /* __ITRACE_H__ */
