/***************************************************************************************
 * itrace.cpp - 指令执行追踪 (Instruction Trace) 实现 for NPC2
 ***************************************************************************************/

#include "itrace.h"
#include "disasm.h"

// 全局实例
ITrace itrace;

/**
 * write - 记录一条指令执行信息
 */
void ITrace::write(uint32_t pc, uint32_t inst, uint64_t cycle) {
    entries[head].pc = pc;
    entries[head].inst = inst;
    entries[head].cycle = cycle;
    entries[head].valid = true;

#if ITRACE_REALTIME
    // ITRACE_REALTIME模式: 实时打印每条指令
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
    printf("%s\n", logbuf);
#endif

    // 记录当前位置
    curr = head;

    // 移动到下一个位置（环形）
    head = (head + 1) % ITRACE_BUF_SIZE;
}

/**
 * display_ringbuf - 显示环形缓冲区的内容
 */
void ITrace::display_ringbuf() {
#if !ITRACE_REALTIME
    if (curr < 0) {
        printf("[itrace] No instructions recorded.\n");
        return;
    }

    printf("\n========== Instruction Trace Ring Buffer ==========\n");

    int count = 0;
    char logbuf[INST_LOG_SIZE]; // 临时缓冲区

    for (int i = 0; i < ITRACE_BUF_SIZE; i++) {
        int idx = (head + i) % ITRACE_BUF_SIZE;
        if (entries[idx].valid) {
            // 格式化日志 (Lazy Disassembly)
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

            // 用 "-->" 标记最后执行的指令
            const char *marker = (idx == curr) ? " --> " : "     ";
            printf("%s%s\n", marker, logbuf);
            count++;
        }
    }

    printf("========== End of Instruction Trace (%d instructions) ==========\n", count);
#endif
}
