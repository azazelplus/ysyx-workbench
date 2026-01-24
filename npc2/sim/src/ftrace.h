/***************************************************************************************
 * ftrace.h - 函数调用追踪 (Function Trace) for NPC2
 * 
 * 功能：
 * 1. 解析 ELF 文件获取函数符号表
 * 2. 追踪 jal/jalr/ret 指令，记录函数调用/返回
 * 3. 实时打印（REALTIME模式）或环形缓冲区（非REALTIME模式）
 * 
 * 配置（在 main.cpp 中）：
 * - ENABLE_FTRACE: 编译时开关，false 时整个功能不编译
 * - REALTIME_FTRACE: 运行时模式，true=实时打印，false=环形缓冲区
 ***************************************************************************************/

#ifndef __FTRACE_H__
#define __FTRACE_H__

#include <cstdint>
#include <cstdio>
#include <cstring>

// 配置应在 config.h 中定义，并在 include 此文件前先 include config.h
#ifndef ENABLE_FTRACE
#error "Please include config.h before ftrace.h"
#endif

#if ENABLE_FTRACE

#include <vector>
#include <string>

// 环形缓冲区大小（函数调用记录条数）
#define FTRACE_BUF_SIZE 64

// 单条日志缓冲区大小
#define FTRACE_LOG_SIZE 256

// 最大调用深度（用于缩进显示）
#define FTRACE_MAX_DEPTH 64

// 函数符号信息
struct FuncSymbol {
    uint32_t addr;      // 函数起始地址
    uint32_t size;      // 函数大小（字节）
    std::string name;   // 函数名
};

// 调用类型
enum FTraceType {
    FTRACE_CALL = 0,    // 函数调用
    FTRACE_RET  = 1     // 函数返回
};

// 环形缓冲区条目
struct FTraceEntry {
    uint32_t pc;            // 调用/返回指令的 PC
    uint32_t target;        // 目标地址（调用时为被调用函数地址，返回时为返回地址）
    uint64_t cycle;         // 仿真周期
    FTraceType type;        // 调用/返回
    int depth;              // 调用深度
    std::string func_name;  // 函数名
    bool valid;
};

// 函数追踪类
class FTrace {
private:
    // 符号表
    std::vector<FuncSymbol> symbols;
    
    // 环形缓冲区
    FTraceEntry entries[FTRACE_BUF_SIZE];
    int head;
    int curr;
    
    // 配置
    bool is_realtime;   // 是否实时打印
    
    // 调用深度
    int call_depth;
    
    // 根据地址查找函数名
    const char* find_func(uint32_t addr);
    
    // 格式化日志
    void format_log(char *buf, size_t size, const FTraceEntry &e);

public:
    FTrace();
    
    /**
     * 配置实时打印模式
     */
    void set_realtime(bool real) { is_realtime = real; }
    
    /**
     * init_elf - 从 ELF 文件加载符号表
     * @param elf_path: ELF 文件路径
     * @return: 成功返回 true
     */
    bool init_elf(const char* elf_path);
    
    /**
     * trace - 追踪一条指令，判断是否为函数调用/返回
     * @param pc: 当前 PC
     * @param inst: 当前指令
     * @param next_pc: 下一条指令的 PC（用于判断跳转目标）
     * @param cycle: 仿真周期
     */
    void trace(uint32_t pc, uint32_t inst, uint32_t next_pc, uint64_t cycle);
    
    /**
     * display_ringbuf - 显示环形缓冲区内容
     */
    void display_ringbuf();
    
    /**
     * 获取符号数量（用于调试）
     */
    size_t get_symbol_count() const { return symbols.size(); }
};

#else  // !ENABLE_FTRACE

// 空实现：当 ENABLE_FTRACE = false 时，编译空壳类以避免链接错误
class FTrace {
public:
    void set_realtime(bool real) {}
    bool init_elf(const char* elf_path) { return false; }
    void trace(uint32_t pc, uint32_t inst, uint32_t next_pc, uint64_t cycle) {}
    void display_ringbuf() {}
    size_t get_symbol_count() const { return 0; }
};

#endif  // ENABLE_FTRACE

// 全局实例
extern FTrace ftrace;

#endif /* __FTRACE_H__ */