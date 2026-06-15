/***************************************************************************************
 * ftrace.c 
 * 挂载点: 
 * 1.ftrace_init()在init_monitor()(monitor.c)中被调用. 加载ELF文件.
 * 2.exec_once()->trace_and_difftest()中调用ftrace_trace().

 * 3.
 * 
 * 功能：
 *   1. 解析 ELF 文件获取函数符号表
 *   2. 追踪 jal/jalr/ret 指令，记录函数调用/返回
 *   3. 环形缓冲区模式：程序异常时打印最近的函数调用链
 ***************************************************************************************/
#include <common.h>   // CONFIG_FTRACE宏是common.h中定义的



#ifdef CONFIG_FTRACE //一直到文件结尾...

#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============ 常量定义 ============

#define FTRACE_MAX_SYMBOLS  4096   // 最多支持的函数符号数量
#define FTRACE_MAX_NAME     128    // 函数名最大长度
#ifdef CONFIG_FTRACE_RINGBUF
#define FTRACE_BUF_SIZE     CONFIG_FTRACE_RINGBUF_SIZE  // 环形缓冲区大小（条目数），由 Kconfig FTRACE_RINGBUF_SIZE 控制
#endif
#define FTRACE_LOG_SIZE     512    // 单条日志缓冲区大小（需容纳：12 + indent(128) + 6 + funcname(127) + 1 = 274 字节最坏情况）
#define FTRACE_MAX_DEPTH    64     // 最大调用深度（用于缩进）

// ============ 数据结构 ============

typedef struct {
  uint32_t addr;                    //函数起始地址(来自该符号结构体(Elf32_Sym)的.st_value成员)
  uint32_t size;                    //函数字节大小(来自st_size)
  char     name[FTRACE_MAX_NAME];   //函数名字符串(来自字符串表strtab. 拿到st_name后)
} FuncSymbol; //一个函数符号的结构体. 后续将ELF文件符号表中所有STT_FUNC类型的符号压缩成这个结构存储.

/***************  补充: ELF标准库函数 ********************
ELF文件在库函数的处理方式: ELF是多个section组成的. 每个section的存储对应一个结构体:
ELF文件
  ├─ ELF Header: 1个Elf32_Ehdr结构体
  │   ├─ e_ident[]        // 魔数 + 文件类型标志
  │   ├─ e_shoff          // Section Header Table 的文件偏移
  │   ├─ e_shnum          // Section Header 的个数
  │   └─ e_shstrndx       // Section 名字字符串表的索引  
  ├─ Section Header Table: Elf32_Shdr[]结构体数组
  │   ├─ Elf32_Shdr[0]
  │   │  └─ sh_offset = 4096   ← ".text" 的实际数据在文件偏移 4096
  │   ├─ Elf32_Shdr[1]
  │   │  └─ sh_offset = 8192   ← ".data" 的实际数据在文件偏移 8192
  │   ├─ Elf32_Shdr[i]
  │   │  └─ sh_offset = 2000   ← ".symtab" 的实际数据在文件偏移 2000
  │   └─ ...
  ├─ .symbol(符号表section): Elf32_Sym结构体数组
  │   └─ Elf32_Sym[0]  ← 第 1 个符号
  │   └─ Elf32_Sym[1]  ← 第 2 个符号
  │   └─ Elf32_Sym[2]  ← 第 3 个符号
  │   └─ ...
  ├─ .strtab(符号名字符串表): char[]结构体
  └─ ...
  
标准库用*Elf32_Sym结构体*存储符号表中的一个符号.  它有包含的信息有:

typedef struct {
  uint32_t      st_name;    // 符号名在 .strtab 中的偏移
  Elf32_Addr    st_value;   // 符号值（通常是地址）
  uint32_t      st_size;    // 符号大小（字节数）
  unsigned char st_info;    // 1byte,  高4bit是符号绑定属性, 低4bit是符号类型.
  unsigned char st_other;   
  uint16_t      st_shndx;   // 符号所在 section 的索引
} Elf32_Sym;    
 
标准库给st_info的低四位提供了宏定义:

#define STT_NOTYPE   0  // 未知类型
#define STT_OBJECT   1  // 数据对象（全局变量）
#define STT_FUNC     2  // 函数
#define STT_SECTION  3  // section
#define STT_FILE     4  // 源文件
...

一个STT_FUNC类型符号 == 一个函数符号. 就是.st_info低4bit=2的符号, 它表示这个符号指向一个函数.
*/


typedef enum {
  FTRACE_CALL = 0,
  FTRACE_RET  = 1
} FTraceType;   //只有两个值, 区分是进入函数(call)还是离开函数(ret)

typedef struct {
  uint32_t   pc;      // 发出jal/jalr的指令地址
  uint32_t   target;  // 跳转目标地址. call 时=被调函数入口，ret 时=返回地址
  FTraceType type;    // FTRACE_CALL=0 或 FTRACE_RET=1
  int        depth;   // 记录本条时的调用深度
  char       func_name[FTRACE_MAX_NAME];//函数名
  bool       valid;   //有效位
} FTraceEntry; //环形缓冲区中的一条记录.


// ============ 全局状态 ============
static FuncSymbol  symbols[FTRACE_MAX_SYMBOLS];   // 整个ELF文件的所有符号表数组. 其中每个FuncSymbol结构体成员记录一个符号.
static int         sym_count = 0;                 // 已加载的符号数量

#ifdef CONFIG_FTRACE_RINGBUF
static FTraceEntry entries[FTRACE_BUF_SIZE];      // 环形缓冲区数组. 每个成员记录一条记录.
static int         ftrace_head = 0;               // 下次写入的槽位. 写完后 +1 取 FTRACE_BUF_SIZE 模
static int         ftrace_curr = -1;              // 最后一次写入的槽位. -1 表示没有记录.
#endif

static int call_depth = 0;            //当前调用深度



// ============ ELF 解析函数 ============
/**
 * read_section - 从 ELF 文件中读取一个 section 的原始数据.
 * 其实就是封装了 malloc + fseek + fread 的完整流程，
 * 失败时自动释放内存并返回 NULL，简化了调用方的错误处理。
 * 
 * @param fp     已打开且指针在文件开头的 FILE* 指针
 * @param offset 待读 section 相对文件开头的偏移字节数（来自 ELF header）
 * @param size   待读 section 的字节数（来自 ELF header 的 sh_size）
 * 
 * @return 指向堆分配数据的指针（调用者需手动 free）；读取失败返回 NULL
 */
static void *read_section(FILE *fp, long offset, size_t size) {
  void *buf = malloc(size); //在堆上开辟一块内存buf, 存放一会儿要读的section数据.
  if (!buf) return NULL;    // 检查是否分配成功
  fseek(fp, offset, SEEK_SET);  //文件指针(内部光标)移动到待读取section在elf文件中的offset. 

  // 从fseek移动好的光标位置, 读取size个byte, 将内容存入buf堆内存空间.
  if (fread(buf, 1, size, fp) != size) {
    free(buf);
    return NULL;
  }
  return buf;
}

/**
 * parse_elf32 - 解析 32 位 ELF 文件的符号表
 * 
 * 从已打开的 ELF32 文件中读取 .symtab 和 .strtab section，
 * 提取所有 STT_FUNC 类型的符号，填充全局 symbols[] 数组。
 * 
 * @param fp 已打开的 ELF32 文件指针，指针将被重新定位
 * 
 * @return 成功返回 true（至少加载了 1 个符号），失败返回 false
 */
static bool parse_elf32(FILE *fp) {
  Elf32_Ehdr ehdr;
  fseek(fp, 0, SEEK_SET);
  if (fread(&ehdr, sizeof(ehdr), 1, fp) != 1) return false;
  if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) return false;
  if (ehdr.e_ident[EI_CLASS] != ELFCLASS32) return false;

  // 读 Section Header Table
  Elf32_Shdr *shdrs = read_section(fp, ehdr.e_shoff,
                                   ehdr.e_shnum * sizeof(Elf32_Shdr));
  if (!shdrs) return false;

  // 读 Section 名字符串表，用于按名字查找 .symtab / .strtab
  Elf32_Shdr *shstrtab_sh = &shdrs[ehdr.e_shstrndx];
  char *shstrtab = read_section(fp, shstrtab_sh->sh_offset, shstrtab_sh->sh_size);
  if (!shstrtab) { free(shdrs); return false; }

  // 按名字找 .symtab 和 .strtab
  Elf32_Shdr *symtab_sh = NULL, *strtab_sh = NULL;
  for (int i = 0; i < ehdr.e_shnum; i++) {
    const char *name = &shstrtab[shdrs[i].sh_name];
    if      (strcmp(name, ".symtab") == 0) symtab_sh = &shdrs[i];
    else if (strcmp(name, ".strtab") == 0) strtab_sh = &shdrs[i];
  }
  if (!symtab_sh || !strtab_sh) { free(shstrtab); free(shdrs); return false; }

  // 读 .strtab（符号名字符串表）和 .symtab（符号表）
  char      *strtab = read_section(fp, strtab_sh->sh_offset, strtab_sh->sh_size);
  Elf32_Sym *syms   = read_section(fp, symtab_sh->sh_offset, symtab_sh->sh_size);
  if (!strtab || !syms) { free(syms); free(strtab); free(shstrtab); free(shdrs); return false; }

  // 提取类型为 STT_FUNC 的符号
  int sym_num = symtab_sh->sh_size / sizeof(Elf32_Sym);
  for (int i = 0; i < sym_num && sym_count < FTRACE_MAX_SYMBOLS; i++) {
    if (ELF32_ST_TYPE(syms[i].st_info) == STT_FUNC && syms[i].st_size > 0) {
      symbols[sym_count].addr = syms[i].st_value;
      symbols[sym_count].size = syms[i].st_size;
      strncpy(symbols[sym_count].name, &strtab[syms[i].st_name], FTRACE_MAX_NAME - 1);
      symbols[sym_count].name[FTRACE_MAX_NAME - 1] = '\0';
      sym_count++;
    }
  }

  free(syms); free(strtab); free(shstrtab); free(shdrs);
  return true;
}

/**
 * parse_elf64 - 解析 64 位 ELF 文件的符号表
 * 
 * 从已打开的 ELF64 文件中读取 .symtab 和 .strtab section，
 * 提取所有 STT_FUNC 类型的符号，填充全局 symbols[] 数组。
 * 
 * @param fp 已打开的 ELF64 文件指针，指针将被重新定位
 * 
 * @return 成功返回 true（至少加载了 1 个符号），失败返回 false
 */
static bool parse_elf64(FILE *fp) {
  Elf64_Ehdr ehdr;
  fseek(fp, 0, SEEK_SET);
  if (fread(&ehdr, sizeof(ehdr), 1, fp) != 1) return false;
  if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) return false;
  if (ehdr.e_ident[EI_CLASS] != ELFCLASS64) return false;

  Elf64_Shdr *shdrs = read_section(fp, ehdr.e_shoff,
                                   ehdr.e_shnum * sizeof(Elf64_Shdr));
  if (!shdrs) return false;

  Elf64_Shdr *shstrtab_sh = &shdrs[ehdr.e_shstrndx];
  char *shstrtab = read_section(fp, shstrtab_sh->sh_offset, shstrtab_sh->sh_size);
  if (!shstrtab) { free(shdrs); return false; }

  Elf64_Shdr *symtab_sh = NULL, *strtab_sh = NULL;
  for (int i = 0; i < ehdr.e_shnum; i++) {
    const char *name = &shstrtab[shdrs[i].sh_name];
    if      (strcmp(name, ".symtab") == 0) symtab_sh = &shdrs[i];
    else if (strcmp(name, ".strtab") == 0) strtab_sh = &shdrs[i];
  }
  if (!symtab_sh || !strtab_sh) { free(shstrtab); free(shdrs); return false; }

  char      *strtab = read_section(fp, strtab_sh->sh_offset, strtab_sh->sh_size);
  Elf64_Sym *syms   = read_section(fp, symtab_sh->sh_offset, symtab_sh->sh_size);
  if (!strtab || !syms) { free(syms); free(strtab); free(shstrtab); free(shdrs); return false; }

  int sym_num = symtab_sh->sh_size / sizeof(Elf64_Sym);
  for (int i = 0; i < sym_num && sym_count < FTRACE_MAX_SYMBOLS; i++) {
    if (ELF64_ST_TYPE(syms[i].st_info) == STT_FUNC && syms[i].st_size > 0) {
      symbols[sym_count].addr = (uint32_t)syms[i].st_value;
      symbols[sym_count].size = (uint32_t)syms[i].st_size;
      strncpy(symbols[sym_count].name, &strtab[syms[i].st_name], FTRACE_MAX_NAME - 1);
      symbols[sym_count].name[FTRACE_MAX_NAME - 1] = '\0';
      sym_count++;
    }
  }

  free(syms); free(strtab); free(shstrtab); free(shdrs);
  return true;
}

// ============ 公开接口 ============

/**
 * ftrace_init - 从 ELF 文件加载函数符号表. 包裹fopen(), parse_elf32()
 * 
 * 这是 ftrace 的初始化函数，应在程序启动时由 monitor.c 调用（通过 --elf 参数传入）。
 * 函数会按以下顺序尝试解析：先 ELF32，失败则尝试 ELF64。
 * 成功加载后会打印符号数量到标准输出，失败则打印错误信息。
 * 
 * @param elf_path ELF 文件的完整路径；为 NULL 或空串时直接返回（不加载任何符号）
 * 
 * @return 无返回值（void），成功/失败状态通过 printf 输出
 */
void ftrace_init(const char *elf_path) {
  if (!elf_path || *elf_path == '\0') return;

  FILE *fp = fopen(elf_path, "rb");
  if (!fp) {
    printf("[ftrace] Cannot open ELF file: %s\n", elf_path);
    return;
  }

  sym_count = 0;
  bool ok = parse_elf32(fp);
  if (!ok) {
    sym_count = 0;
    ok = parse_elf64(fp);
  }
  fclose(fp);

  if (ok) {
    printf("[ftrace] Loaded %d function symbols from %s\n", sym_count, elf_path);
  } else {
    printf("[ftrace] Failed to load symbols from %s\n", elf_path);
  }
}

// ftrace_find_func 仅在环形缓冲区或全量日志至少一个启用时才会被调用
#if defined(CONFIG_FTRACE_RINGBUF) || defined(CONFIG_FTRACE_LOG)
/**
 * ftrace_find_func - 根据地址查找函数名
 * 
 * 在已加载的符号表中进行线性查找，返回包含指定地址的第一个函数的名字。
 * 符号存储为 [addr, addr+size) 的开区间，查找使用 addr <= query < addr+size。
 * 
 * @param addr 待查询的地址（通常是 jal/jalr 指令的跳转目标）
 * 
 * @return 找到的函数名指针（指向 symbols 数组中的 name 字段）；
 *         未找到返回 "<unknown>" 的静态字符串
 */
static const char* ftrace_find_func(uint32_t addr) {
  for (int i = 0; i < sym_count; i++) {
    if (addr >= symbols[i].addr && addr < symbols[i].addr + symbols[i].size) {
      return symbols[i].name;
    }
  }
  return "<unknown>";
}
#endif // CONFIG_FTRACE_RINGBUF || CONFIG_FTRACE_LOG

// ftrace_format 仅在环形缓冲区或全量日志至少一个启用时才会被调用
#if defined(CONFIG_FTRACE_RINGBUF) || defined(CONFIG_FTRACE_LOG)
/**
 * ftrace_format - 格式化一条 ftrace 记录为可打印的字符串
 * 
 * 将 FTraceEntry 结构体转换为人类可读的日志行，包含：
 * - PC 地址（十六进制）
 * - 调用深度对应的缩进（每层 2 个空格）
 * - "call" 或 "ret" 指令标签
 * - 函数名及（对于 call）目标地址
 * 
 * @param buf    输出缓冲区（预留至少 FTRACE_LOG_SIZE=512 字节）
 * @param size   缓冲区大小（字节）
 * @param e      待格式化的 FTraceEntry 指针
 * 
 * @return 无返回值（void），格式化结果写入 buf
 */
static void ftrace_format(char *buf, size_t size, const FTraceEntry *e) {
  char indent[FTRACE_MAX_DEPTH * 2 + 1];
  int len = e->depth * 2;
  if (len > FTRACE_MAX_DEPTH * 2) len = FTRACE_MAX_DEPTH * 2;
  memset(indent, ' ', len);
  indent[len] = '\0';

  if (e->type == FTRACE_CALL) {
    snprintf(buf, size, "0x%08x: %scall [%s@0x%08x]",
             e->pc, indent, e->func_name, e->target);
  } else {
    snprintf(buf, size, "0x%08x: %sret  [%s]",
             e->pc, indent, e->func_name);
  }
}
#endif // CONFIG_FTRACE_RINGBUF || CONFIG_FTRACE_LOG


/**
 * ftrace_trace - 追踪一条指令，识别 call/ret. 
 * 
 * 这是 ftrace 的核心追踪函数，应在每条指令执行后由 cpu-exec.c 调用。
 * 函数会解码当前指令的 opcode、rd、rs1，判断是 jal/jalr/其他
 * 若是函数调用则 call_depth++，若是函数返回则 call_depth--，
 * 然后根据编译配置驱动以下两条独立的输出路径（可同时启用）：
 * 
 *   - CONFIG_FTRACE_RINGBUF：将 FTraceEntry 写入静态环形缓冲区 entries[]，
 *     不立即打印；崩溃时由 ftrace_display() 一次性 dump 到 stdout。
 * 
 *   - CONFIG_FTRACE_LOG：将格式化后的日志行通过 log_write() 实时写入
 *     log_fp（-l 指定的文件，或 stdout），每条 call/ret 立即落盘。
 * 
 * @param pc      当前指令的 PC（程序计数器）值
 * @param inst    当前指令的 32 位编码（原始机器码）
 * @param next_pc 指令执行后的下一 PC（dnpc，即跳转目标或 pc+4）
 * 
 * @return 无返回值（void），若 CONFIG_FTRACE_COND_EXPR 为 false 或非 call/ret 则直接返回
 */
void ftrace_trace(uint32_t pc, uint32_t inst, uint32_t next_pc) {
  if (!(CONFIG_FTRACE_COND_EXPR)) return;

  // ---- 解码指令字段 ----
  // RISC-V 32位指令格式（所有格式共用低7位 opcode）：
  //   [6:0]   opcode  —— 指令类型
  //   [11:7]  rd      —— 目标寄存器（写入结果的寄存器）
  //   [19:15] rs1     —— 第一个源寄存器
  uint32_t opcode = inst & 0x7F;
  uint32_t rd     = (inst >> 7)  & 0x1F;
  uint32_t rs1    = (inst >> 15) & 0x1F;

  bool is_call = false, is_ret = false;

  // ---- 依据 RISC-V ABI calling convention 判断 call/ret ----
  //
  // JAL (opcode=0x6F): rd = pc+4; pc = pc + imm.
  // JAL只可能代表函数调用(call)和可忽略的普通跳转, 由rd ?= x0决定:
  //   rd != x0 → 把返回地址保存到 rd，是函数调用(call). 典型形式: jal ra, <func>（ra = x1 = 返回地址寄存器）
  //   rd == x0 → 返回地址丢弃，仅是无条件跳转（j 伪指令），不是函数边界，忽略
  if (opcode == 0x6F) {          // JAL
    if (rd != 0) { is_call = true; }

  // JALR (opcode=0x67): rd = pc+4; pc = rs1 + imm.
  //  JALR可以代表ret, call或可忽略的普通跳转. 由rd（返回地址存哪里）和 rs1（往哪里跳）决定:
  //   rd==x0, rs1==x1(ra) → 跳回 ra 中保存的调用者地址, 且不保存当前pc. → ret
  //   rd!=x0              → 说明要保存当前pc, 所以是函数调用(间接跳转). → call
  //   rd==x0, rs1!=x1     → 不保存当前pc也间接跳转(如 switch 跳表 jr t1), 不是函数边界, 可以忽略的一般跳转行为.
  } else if (opcode == 0x67) {   // JALR
    if (rd == 0 && rs1 == 1) {   // ret: jalr x0, ra, 0
      is_ret = true;
    } else if (rd != 0) {        // 间接 call: jalr ra, t0, 0 等
      is_call = true;
    }
  }

  if (!is_call && !is_ret) return;  //对于一般跳转, ftrace_trace()直接返回啥也不做.

#if defined(CONFIG_FTRACE_RINGBUF) || defined(CONFIG_FTRACE_LOG)
  FTraceEntry e;
  e.pc     = pc;
  e.target = next_pc;
  e.valid  = true;

  if (is_call) {
    e.type  = FTRACE_CALL;
    e.depth = call_depth;
    strncpy(e.func_name, ftrace_find_func(next_pc), FTRACE_MAX_NAME - 1);
    e.func_name[FTRACE_MAX_NAME - 1] = '\0';
    call_depth++;
  } else {
    if (call_depth > 0) call_depth--;
    e.type  = FTRACE_RET;
    e.depth = call_depth;
    strncpy(e.func_name, ftrace_find_func(pc), FTRACE_MAX_NAME - 1);
    e.func_name[FTRACE_MAX_NAME - 1] = '\0';
  }
#else
  // 环形缓冲区和全量日志都未启用，只需维护 call_depth
  if (is_call) call_depth++;
  else if (call_depth > 0) call_depth--;
#endif

#ifdef CONFIG_FTRACE_RINGBUF
  // 写入环形缓冲区
  entries[ftrace_head] = e;
  ftrace_curr = ftrace_head;
  ftrace_head = (ftrace_head + 1) % FTRACE_BUF_SIZE;
#endif

#ifdef CONFIG_FTRACE_LOG
  // 全量日志：同 ITRACE 架构，通过 log_write 写入 log_fp（文件或 stdout）
  // log_enable() 控制是否在 [CONFIG_TRACE_START, CONFIG_TRACE_END] 范围内记录
  char logline[FTRACE_LOG_SIZE];
  ftrace_format(logline, sizeof(logline), &e);
  log_write("[ftrace] %s\n", logline);
#endif
}

/**
 * ftrace_display - 打印环形缓冲区内容（程序异常时调用）
 * 
 * 按时间顺序（从 ftrace_head 开始、到 ftrace_head-1 结束）打印环形缓冲区中的所有有效项，
 * 用 " --> " 前缀标记最后一条记录（ftrace_curr）。
 * 若缓冲区为空（ftrace_curr == -1）则打印提示信息。
 * 
 * 这个函数应在程序发生 BAD TRAP（halt_ret != 0）或 ABORT 异常时由 cpu-exec.c 调用，
 * 帮助开发者诊断程序崩溃前的函数调用链。
 * 
 * @return 无返回值（void），所有输出通过 printf 到标准输出
 */
void ftrace_display() {
#ifdef CONFIG_FTRACE_RINGBUF
  if (ftrace_curr < 0) {
    printf("[ftrace] No function calls recorded.\n");
    return;
  }

  printf("\n========== Function Trace Ring Buffer ==========\n");
  char logbuf[FTRACE_LOG_SIZE];
  int count = 0;

  for (int i = 0; i < FTRACE_BUF_SIZE; i++) {
    int idx = (ftrace_head + i) % FTRACE_BUF_SIZE;
    if (entries[idx].valid) {
      ftrace_format(logbuf, sizeof(logbuf), &entries[idx]);
      const char *marker = (idx == ftrace_curr) ? " --> " : "     ";
      printf("%s%s\n", marker, logbuf);
      count++;
    }
  }

  printf("========== End of Function Trace (%d records) ==========\n", count);
#else
  // FTRACE_RINGBUF 未启用，无环形缓冲区内容可打印
  printf("[ftrace] Ring buffer disabled (enable CONFIG_FTRACE_RINGBUF to see crash trace).\n");
#endif
}

#endif // CONFIG_FTRACE
