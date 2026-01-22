/***************************************************************************************
 * mtrace.cpp - 内存访问追踪 (Memory Access Trace) 实现 for NPC2
 ***************************************************************************************/

#include "mtrace.h"

// 全局实例
MTrace mtrace;

MTrace::MTrace() : head(0), curr(-1), is_enabled(true), is_realtime(false), range_enabled(false), range_start(0), range_end(0) {
    for (int i = 0; i < MTRACE_BUF_SIZE; i++) {
        entries[i].valid = false;
    }
}

// 检查地址是否在追踪范围内
bool MTrace::in_range(uint32_t addr) {
    if (range_enabled) {
        return (addr >= range_start && addr < range_end);
    }
    return true;
}

// 格式化日志
void MTrace::format_log(char *buf, size_t size, const MTraceEntry &e) {
    const char *type_str = (e.type == MTRACE_READ) ? "READ " : "WRITE";
    snprintf(buf, size,
             "[CYCLE %5lu] %s: addr=0x%08x, len=%d, data=0x%08x, pc=0x%08x",
             e.cycle, type_str, e.addr, e.len, e.data, e.pc);
}

/**
 * write - 记录一次内存访问
 */
void MTrace::write(uint32_t addr, uint32_t data, uint8_t len, MTraceType type, 
           uint32_t pc, uint64_t cycle) {
    
    if (!is_enabled) return;

    // 范围过滤
    if (!in_range(addr)) return;

    entries[head].addr = addr;
    entries[head].data = data;
    entries[head].len = len;
    entries[head].type = type;
    entries[head].pc = pc;
    entries[head].cycle = cycle;
    entries[head].valid = true;

    if (is_realtime) {
        char logbuf[MTRACE_LOG_SIZE];
        format_log(logbuf, sizeof(logbuf), entries[head]);
        printf("[mtrace] %s\n", logbuf);
    }

    curr = head;
    head = (head + 1) % MTRACE_BUF_SIZE;
}

/**
 * display_ringbuf - 显示环形缓冲区内容
 */
void MTrace::display_ringbuf() {
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
}
