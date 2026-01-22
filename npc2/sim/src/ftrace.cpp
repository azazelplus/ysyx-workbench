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

#include "ftrace.h"
#include <elf.h>

// 全局实例
FTrace ftrace;

// ============ ELF 解析辅助函数 ============

/**
 * 读取 ELF32 文件的符号表
 */
static bool parse_elf32(FILE* fp, std::vector<FuncSymbol>& symbols) {
    Elf32_Ehdr ehdr;
    
    // 1. 读取 ELF Header
    fseek(fp, 0, SEEK_SET);
    if (fread(&ehdr, sizeof(ehdr), 1, fp) != 1) {
        printf("[ftrace] Failed to read ELF header\n");
        return false;
    }
    
    // 验证 ELF 魔数
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
        printf("[ftrace] Invalid ELF magic number\n");
        return false;
    }
    
    // 验证是 32 位 ELF
    if (ehdr.e_ident[EI_CLASS] != ELFCLASS32) {
        printf("[ftrace] Not a 32-bit ELF file\n");
        return false;
    }
    
    printf("[ftrace] ELF Header: e_shoff=%u, e_shnum=%u, e_shstrndx=%u\n",
           ehdr.e_shoff, ehdr.e_shnum, ehdr.e_shstrndx);
    
    // 2. 读取 Section Header Table
    std::vector<Elf32_Shdr> shdrs(ehdr.e_shnum);
    fseek(fp, ehdr.e_shoff, SEEK_SET);
    if (fread(shdrs.data(), sizeof(Elf32_Shdr), ehdr.e_shnum, fp) != ehdr.e_shnum) {
        printf("[ftrace] Failed to read section headers\n");
        return false;
    }
    
    // 3. 读取 Section 名字符串表（用于找到 .symtab 和 .strtab）
    Elf32_Shdr& shstrtab = shdrs[ehdr.e_shstrndx];
    std::vector<char> shstrtab_data(shstrtab.sh_size);
    fseek(fp, shstrtab.sh_offset, SEEK_SET);
    if (fread(shstrtab_data.data(), 1, shstrtab.sh_size, fp) != shstrtab.sh_size) {
        printf("[ftrace] Failed to read section name string table\n");
        return false;
    }
    
    // 4. 查找 .symtab 和 .strtab
    Elf32_Shdr* symtab = nullptr;
    Elf32_Shdr* strtab = nullptr;
    
    for (size_t i = 0; i < shdrs.size(); i++) {
        const char* name = &shstrtab_data[shdrs[i].sh_name];
        if (strcmp(name, ".symtab") == 0) {
            symtab = &shdrs[i];
            printf("[ftrace] Found .symtab at section %zu, offset=%u, size=%u\n",
                   i, shdrs[i].sh_offset, shdrs[i].sh_size);
        } else if (strcmp(name, ".strtab") == 0) {
            strtab = &shdrs[i];
            printf("[ftrace] Found .strtab at section %zu, offset=%u, size=%u\n",
                   i, shdrs[i].sh_offset, shdrs[i].sh_size);
        }
    }
    
    if (!symtab || !strtab) {
        printf("[ftrace] Could not find .symtab or .strtab\n");
        return false;
    }
    
    // 5. 读取字符串表
    std::vector<char> strtab_data(strtab->sh_size);
    fseek(fp, strtab->sh_offset, SEEK_SET);
    if (fread(strtab_data.data(), 1, strtab->sh_size, fp) != strtab->sh_size) {
        printf("[ftrace] Failed to read string table\n");
        return false;
    }
    
    // 6. 读取符号表，提取函数符号
    size_t sym_count = symtab->sh_size / sizeof(Elf32_Sym);
    std::vector<Elf32_Sym> syms(sym_count);
    fseek(fp, symtab->sh_offset, SEEK_SET);
    if (fread(syms.data(), sizeof(Elf32_Sym), sym_count, fp) != sym_count) {
        printf("[ftrace] Failed to read symbol table\n");
        return false;
    }
    
    printf("[ftrace] Symbol table has %zu entries\n", sym_count);
    
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
    
    printf("[ftrace] Loaded %zu function symbols\n", symbols.size());
    
    // 打印前几个函数（调试用）
    int print_count = std::min((size_t)10, symbols.size());
    for (int i = 0; i < print_count; i++) {
        printf("[ftrace]   [%d] 0x%08x - 0x%08x: %s\n",
               i, symbols[i].addr, symbols[i].addr + symbols[i].size, symbols[i].name.c_str());
    }
    if (symbols.size() > 10) {
        printf("[ftrace]   ... (%zu more)\n", symbols.size() - 10);
    }
    
    return true;
}

// ============ FTrace 类实现 ============

FTrace::FTrace() 
    : head(0), curr(-1), is_enabled(true), is_realtime(false), call_depth(0) {
    for (int i = 0; i < FTRACE_BUF_SIZE; i++) {
        entries[i].valid = false;
    }
}

bool FTrace::init_elf(const char* elf_path) {
    if (!elf_path || strlen(elf_path) == 0) {
        printf("[ftrace] No ELF file specified, ftrace disabled\n");
        is_enabled = false;
        return false;
    }
    
    FILE* fp = fopen(elf_path, "rb");
    if (!fp) {
        printf("[ftrace] Cannot open ELF file: %s\n", elf_path);
        is_enabled = false;
        return false;
    }
    
    printf("[ftrace] Loading symbols from: %s\n", elf_path);
    
    bool result = parse_elf32(fp, symbols);
    fclose(fp);
    
    if (!result) {
        is_enabled = false;
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

void FTrace::trace(uint32_t pc, uint32_t inst, uint32_t next_pc, uint64_t cycle) {
    if (!is_enabled) return;
    
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
    
    if (!is_call && !is_ret) return;
    
    // 记录到环形缓冲区
    FTraceEntry& e = entries[head];
    e.pc = pc;
    e.target = target;
    e.cycle = cycle;
    e.valid = true;
    
    if (is_call) {
        e.type = FTRACE_CALL;
        e.func_name = find_func(target);
        e.depth = call_depth;
        call_depth++;
    } else {
        call_depth = std::max(0, call_depth - 1);
        e.type = FTRACE_RET;
        e.func_name = find_func(pc);  // 返回时显示当前函数名
        e.depth = call_depth;
    }
    
    if (is_realtime) {
        char logbuf[FTRACE_LOG_SIZE];
        format_log(logbuf, sizeof(logbuf), e);
        printf("[ftrace] %s\n", logbuf);
    }
    
    curr = head;
    head = (head + 1) % FTRACE_BUF_SIZE;
}

void FTrace::display_ringbuf() {
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
