/***************************************************************************************
 * itrace.cpp - 指令执行追踪 (Instruction Trace) 实现 for NPC2
 ***************************************************************************************/

#include "config.h"
#include "itrace.h"

// 全局实例
ITrace itrace;

#if ENABLE_ITRACE

#include "disasm.h"

ITrace::ITrace() : head(0), curr(-1), is_realtime(false) {
    for (int i = 0; i < ITRACE_BUF_SIZE; i++) {
        entries[i].valid = false;
    }
}

/**
 * write - 记录一条指令执行信息
 */
void ITrace::write(uint32_t pc, uint32_t inst, uint64_t cycle) {
    std::string asm_str = disassemble(inst);
    char logbuf[INST_LOG_SIZE];
    snprintf(logbuf, INST_LOG_SIZE,
             "[CYCLE %5lu] PC=0x%08x, INST=0x%08x %02x %02x %02x %02x %s",
             cycle,
             pc,
             inst,
             (inst >> 0) & 0xFF,
             (inst >> 8) & 0xFF,
             (inst >> 16) & 0xFF,
             (inst >> 24) & 0xFF,
             asm_str.c_str());

    if (is_realtime) {
        // REALTIME模式: 实时打印每条指令，不维护环形缓冲区
        printf("[itrace] %s\n", logbuf);
    } else {
        // 非REALTIME模式: 记录到环形缓冲区
        entries[head].pc = pc;
        entries[head].inst = inst;
        entries[head].cycle = cycle;
        entries[head].valid = true;
        curr = head;
        head = (head + 1) % ITRACE_BUF_SIZE;
    }
}

/**
 * display_ringbuf - 显示环形缓冲区的内容
 */
void ITrace::display_ringbuf() {
    if (is_realtime) return;  // REALTIME模式不维护缓冲区
    
    if (curr < 0) {
        printf("[itrace] No instructions recorded.\n");
        return;
    }

    printf("\n========== Instruction Trace Ring Buffer ==========\n");

    int count = 0;
    char logbuf[INST_LOG_SIZE];

    for (int i = 0; i < ITRACE_BUF_SIZE; i++) {
        int idx = (head + i) % ITRACE_BUF_SIZE;
        if (entries[idx].valid) {
            std::string asm_str = disassemble(entries[idx].inst);
            snprintf(logbuf, INST_LOG_SIZE,
                    "[CYCLE %5lu] PC=0x%08x, INST=0x%08x %02x %02x %02x %02x %s",
                    entries[idx].cycle,
                    entries[idx].pc,
                    entries[idx].inst,
                    (entries[idx].inst >> 0) & 0xFF,
                    (entries[idx].inst >> 8) & 0xFF,
                    (entries[idx].inst >> 16) & 0xFF,
                    (entries[idx].inst >> 24) & 0xFF,
                    asm_str.c_str());

            const char *marker = (idx == curr) ? " --> " : "     ";
            printf("%s%s\n", marker, logbuf);
            count++;
        }
    }

    printf("========== End of Instruction Trace (%d instructions) ==========\n", count);
}

#endif  // ENABLE_ITRACE
