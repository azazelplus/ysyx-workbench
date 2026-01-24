/***************************************************************************************
 * ftrace.cpp - 函数调用追踪 (Function Trace) 实现 for NPC2
 * 
 * ELF 文件结构（简化）：
 * +------------------+
 * | ELF Header       |  <- 文件开头，包含魔数、架构信息、Section Header Table 位置
 * +------------------+
 * | Program Headers  |  <- 描述如何加载程序到内存（这里不需要）
 * +------------------+
 * | .text            |  <- 代码段
 * | .data            |  <- 数据段
 * | .symtab          |  <- 符号表（我们需要的）
 * | .strtab          |  <- 字符串表（符号名存储在这里）
 * | ...              |
 * +------------------+
 * | Section Headers  |  <- 描述各个 Section 的位置和大小
 * +------------------+
 * 
 * 解析步骤：
 * 1. 读取 ELF Header，获取 e_shoff (Section Header Table 偏移) 和 e_shstrndx (Section 名字符串表索引)
 * 2. 读取 Section Header Table
 * 3. 找到 .symtab 和 .strtab
 * 4. 从 .symtab 读取所有 STT_FUNC 类型的符号
 ***************************************************************************************/

#include "config.h"
#include "ftrace.h"

// 全局实例
FTrace ftrace;

#if ENABLE_FTRACE

#include <elf.h>
#include <cstdio>
#include <vector>
#include <cstring>
#include <algorithm> // for std::min, std::max

// ============ ELF 解析辅助函数 ============

/**
 * 读取 ELF32 文件的符号表. 
 * @fp: 已打开的 ELF 文件指针
 * @symbols: 存储解析得到的函数符号
 */
static bool parse_elf32(FILE* fp, std::vector<FuncSymbol>& symbols) {
    Elf32_Ehdr ehdr;
    
    // 1. 读取 ELF Header
    fseek(fp, 0, SEEK_SET);
    if (fread(&ehdr, sizeof(ehdr), 1, fp) != 1) {
        return false;
    }
    
    // 验证 ELF 魔数
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
        return false;
    }
    
    // 验证是 32 位 ELF
    if (ehdr.e_ident[EI_CLASS] != ELFCLASS32) {
        return false;
    }
    // 2. 读取 Section Header Table
    std::vector<Elf32_Shdr> shdrs(ehdr.e_shnum);
    fseek(fp, ehdr.e_shoff, SEEK_SET);
    if (fread(shdrs.data(), sizeof(Elf32_Shdr), ehdr.e_shnum, fp) != ehdr.e_shnum) {
        return false;
    }
    
    // 3. 读取 Section 名字符串表（用于找到 .symtab 和 .strtab）
    Elf32_Shdr& shstrtab = shdrs[ehdr.e_shstrndx];
    std::vector<char> shstrtab_data(shstrtab.sh_size);
    fseek(fp, shstrtab.sh_offset, SEEK_SET);
    if (fread(shstrtab_data.data(), 1, shstrtab.sh_size, fp) != shstrtab.sh_size) {
        return false;
    }
    
    // 4. 查找 .symtab 和 .strtab
    Elf32_Shdr* symtab = nullptr;
    Elf32_Shdr* strtab = nullptr;
    
    for (size_t i = 0; i < shdrs.size(); i++) {
        const char* name = &shstrtab_data[shdrs[i].sh_name];
        if (strcmp(name, ".symtab") == 0) {
            symtab = &shdrs[i];
        } else if (strcmp(name, ".strtab") == 0) {
            strtab = &shdrs[i];
        }
    }
    
    if (!symtab || !strtab) {
        return false;
    }
    
    // 5. 读取字符串表
    std::vector<char> strtab_data(strtab->sh_size);
    fseek(fp, strtab->sh_offset, SEEK_SET);
    if (fread(strtab_data.data(), 1, strtab->sh_size, fp) != strtab->sh_size) {
        return false;
    }
    
    // 6. 读取符号表，提取函数符号
    size_t sym_count = symtab->sh_size / sizeof(Elf32_Sym);
    std::vector<Elf32_Sym> syms(sym_count);
    fseek(fp, symtab->sh_offset, SEEK_SET);
    if (fread(syms.data(), sizeof(Elf32_Sym), sym_count, fp) != sym_count) {
        return false;
    }
    
    // 7. 筛选函数符号 (STT_FUNC)
    for (const auto& sym : syms) {
        // ELF32_ST_TYPE 提取符号类型
        if (ELF32_ST_TYPE(sym.st_info) == STT_FUNC && sym.st_size > 0) {
            FuncSymbol fs;
            fs.addr = sym.st_value;
            fs.size = sym.st_size;
            fs.name = &strtab_data[sym.st_name];
            symbols.push_back(fs);
        }
    }
    
    return true;
}

// ============ FTrace 类实现 ============

//构造函数
FTrace::FTrace() 
    : head(0), curr(-1), is_realtime(false), call_depth(0) {
    for (int i = 0; i < FTRACE_BUF_SIZE; i++) {
        entries[i].valid = false;
    }
}

bool FTrace::init_elf(const char* elf_path) {
    if (!elf_path || strlen(elf_path) == 0) {
        return false;
    }
    
    FILE* fp = fopen(elf_path, "rb");
    if (!fp) {
        printf("[ftrace] Cannot open ELF file: %s\n", elf_path);
        return false;
    }
    
    bool result = parse_elf32(fp, symbols);
    fclose(fp);
    
    if (result) {
        printf("[ftrace] Loaded %zu function symbols from %s\n", symbols.size(), elf_path);
    }
    
    return result;
}

const char* FTrace::find_func(uint32_t addr) {
    for (const auto& sym : symbols) {
        if (addr >= sym.addr && addr < sym.addr + sym.size) {
            return sym.name.c_str();
        }
    }
    return "???";
}

void FTrace::format_log(char *buf, size_t size, const FTraceEntry &e) {
    // 生成缩进
    char indent[FTRACE_MAX_DEPTH * 2 + 1];
    int indent_len = std::min(e.depth * 2, FTRACE_MAX_DEPTH * 2);
    memset(indent, ' ', indent_len);
    indent[indent_len] = '\0';
    
    if (e.type == FTRACE_CALL) {
        snprintf(buf, size, "0x%08x: %scall [%s@0x%08x]",
                 e.pc, indent, e.func_name.c_str(), e.target);
    } else {
        snprintf(buf, size, "0x%08x: %sret  [%s]",
                 e.pc, indent, e.func_name.c_str());
    }
}


// trace方法: 追踪一条指令, 判断是否为call/ret, 如果是则记录.
void FTrace::trace(uint32_t pc, uint32_t inst, uint32_t next_pc, uint64_t cycle) {
    // RISC-V 指令解码
    uint32_t opcode = inst & 0x7F;
    uint32_t rd = (inst >> 7) & 0x1F;
    uint32_t rs1 = (inst >> 15) & 0x1F;
    
    bool is_call = false;
    bool is_ret = false;
    uint32_t target = 0;
    
    // JAL: opcode = 1101111 (0x6F)
    // 如果 rd != x0，认为是函数调用（保存返回地址）
    if (opcode == 0x6F) {
        if (rd != 0) {
            is_call = true;
            target = next_pc;  // next_pc 就是跳转目标
        }
    }
    // JALR: opcode = 1100111 (0x67)
    // 如果 rd == x0 且 rs1 == x1 (ra)，认为是函数返回 (ret)
    // 如果 rd != x0，认为是函数调用
    else if (opcode == 0x67) {
        if (rd == 0 && rs1 == 1) {
            // ret: jalr x0, ra, 0
            is_ret = true;
            target = next_pc;  // 返回地址
        } else if (rd != 0) {
            // 间接调用: jalr rd, rs1, imm
            is_call = true;
            target = next_pc;
        }
    }
    // ftrace 的目的就是 追踪函数调用栈，而不是所有指令. 所以它只关心：jal (函数调用);jalr (函数返回或间接调用)
    if (!is_call && !is_ret) return;
    
    // 格式化日志
    char logbuf[FTRACE_LOG_SIZE];
    
    // 构建临时条目用于格式化
    FTraceEntry temp;
    temp.pc = pc;
    temp.target = target;
    temp.cycle = cycle;
    
    if (is_call) {
        temp.type = FTRACE_CALL;
        temp.func_name = find_func(target);
        temp.depth = call_depth;
        call_depth++;
    } else {
        call_depth = std::max(0, call_depth - 1);
        temp.type = FTRACE_RET;
        temp.func_name = find_func(pc);
        temp.depth = call_depth;
    }
    
    // 根据配置决定是否保存到环形缓冲区
    if (!is_realtime) {
        // 非REALTIME模式：维护环形缓冲区（仅在错误时打印）
        FTraceEntry& e = entries[head];
        e = temp;
        e.valid = true;
        curr = head;
        head = (head + 1) % FTRACE_BUF_SIZE;
    } else {
        // REALTIME模式：实时打印，不维护环形缓冲区
        format_log(logbuf, sizeof(logbuf), temp);
        printf("[ftrace] %s\n", logbuf);
    }
}

void FTrace::display_ringbuf() {
    if (is_realtime) return;  // REALTIME模式不维护缓冲区
    
    if (curr < 0) {
        printf("[ftrace] No function calls recorded.\n");
        return;
    }
    
    printf("\n========== Function Trace Ring Buffer ==========\n");
    
    int count = 0;
    char logbuf[FTRACE_LOG_SIZE];
    
    for (int i = 0; i < FTRACE_BUF_SIZE; i++) {
        int idx = (head + i) % FTRACE_BUF_SIZE;
        if (entries[idx].valid) {
            format_log(logbuf, sizeof(logbuf), entries[idx]);
            const char *marker = (idx == curr) ? " --> " : "     ";
            printf("%s%s\n", marker, logbuf);
            count++;
        }
    }
    
    printf("========== End of Function Trace (%d records) ==========\n", count);
}
#endif  // ENABLE_FTRACE