/***************************************************************************************
 * mtrace.h - 内存访问追踪 (Memory Access Trace) for NPC2
 * 
 * 功能：
 * 1. 记录内存读写操作
 * 2. 支持条件过滤（地址范围、读/写类型）
 * 3. 支持实时打印或仅在错误时输出
 * 
 * 配置宏：
 * - CONFIG_MTRACE: 主开关
 * - MTRACE_REALTIME: 1=实时打印, 0=仅记录到缓冲区
 * - MTRACE_RANGE_ENABLE: 1=启用地址范围过滤
 * - MTRACE_RANGE_START/END: 过滤的地址范围
 ***************************************************************************************/

#ifndef __MTRACE_H__
#define __MTRACE_H__

#include <cstdint>
#include <cstdio>
#include <cstring>

// ============ 内存追踪模式配置 ============
// MTRACE_REALTIME = 1: 实时打印每次访存
// MTRACE_REALTIME = 0: 只保存到环形缓冲区，错误/超时时才显示
#define MTRACE_REALTIME 0

// 是否启用地址范围过滤
#define MTRACE_RANGE_ENABLE 0

#if MTRACE_RANGE_ENABLE
#define MTRACE_RANGE_START 0x80000000UL
#define MTRACE_RANGE_END   0x88000000UL
#endif

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

    // 检查地址是否在追踪范围内
    inline bool in_range(uint32_t addr) {
#if MTRACE_RANGE_ENABLE
        return (addr >= MTRACE_RANGE_START && addr < MTRACE_RANGE_END);
#else
        return true;
#endif
    }

    // 格式化日志
    void format_log(char *buf, size_t size, const MTraceEntry &e) {
        const char *type_str = (e.type == MTRACE_READ) ? "READ " : "WRITE";
        snprintf(buf, size,
                 "[CYCLE %5lu] %s: addr=0x%08x, len=%d, data=0x%08x, pc=0x%08x",
                 e.cycle, type_str, e.addr, e.len, e.data, e.pc);
    }

public:
    MTrace() : head(0), curr(-1) {
        for (int i = 0; i < MTRACE_BUF_SIZE; i++) {
            entries[i].valid = false;
        }
    }

    /**
     * write - 记录一次内存访问
     */
    void write(uint32_t addr, uint32_t data, uint8_t len, MTraceType type, 
               uint32_t pc, uint64_t cycle) {
        // 范围过滤
        if (!in_range(addr)) return;

        entries[head].addr = addr;
        entries[head].data = data;
        entries[head].len = len;
        entries[head].type = type;
        entries[head].pc = pc;
        entries[head].cycle = cycle;
        entries[head].valid = true;

#if MTRACE_REALTIME
        char logbuf[MTRACE_LOG_SIZE];
        format_log(logbuf, sizeof(logbuf), entries[head]);
        printf("[mtrace] %s\n", logbuf);
#endif

        curr = head;
        head = (head + 1) % MTRACE_BUF_SIZE;
    }

    /**
     * display_ringbuf - 显示环形缓冲区内容
     */
    void display_ringbuf() {
#if !MTRACE_REALTIME
        if (curr < 0) {
            printf("[mtrace] No memory accesses recorded.\n");
            return;
        }

        printf("\n========== Memory Trace Ring Buffer ==========\n");

        int count = 0;
        char logbuf[MTRACE_LOG_SIZE];

        for (int i = 0; i < MTRACE_BUF_SIZE; i++) {
            int idx = (head + i) % MTRACE_BUF_SIZE;
            if (entries[idx].valid) {
                format_log(logbuf, sizeof(logbuf), entries[idx]);
                const char *marker = (idx == curr) ? " --> " : "     ";
                printf("%s%s\n", marker, logbuf);
                count++;
            }
        }

        printf("========== End of Memory Trace (%d accesses) ==========\n", count);
#endif
    }
};

// 全局实例
extern MTrace mtrace;

#endif /* __MTRACE_H__ */
