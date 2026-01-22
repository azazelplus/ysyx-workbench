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
    
    bool is_enabled;       // 是否启用
    bool is_realtime;      // 是否实时打印

    // 地址过滤配置
    bool range_enabled;
    uint32_t range_start;
    uint32_t range_end;

    // 检查地址是否在追踪范围内
    bool in_range(uint32_t addr);

    // 格式化日志
    void format_log(char *buf, size_t size, const MTraceEntry &e);

public:
    MTrace();

    /**
     * 配置追踪功能
     */
    void enable(bool en) { is_enabled = en; }
    void set_realtime(bool real) { is_realtime = real; }
    
    /**
     * 配置地址过滤范围
     * @param en: 是否开启过滤
     * @param start: 起始地址 (inclusive)
     * @param end: 结束地址 (exclusive)
     */
    void set_range_filter(bool en, uint32_t start = 0, uint32_t end = 0) {
        range_enabled = en;
        range_start = start;
        range_end = end;
    }

    /**
     * write - 记录一次内存访问
     */
    void write(uint32_t addr, uint32_t data, uint8_t len, MTraceType type, 
               uint32_t pc, uint64_t cycle);

    /**
     * display_ringbuf - 显示环形缓冲区内容
     */
    void display_ringbuf();
};

// 全局实例
extern MTrace mtrace;

#endif /* __MTRACE_H__ */
