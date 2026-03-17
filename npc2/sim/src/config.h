/***************************************************************************************
 * config.h - NPC2 仿真配置
 * 
 * 用户配置区域：修改下方宏定义来控制仿真功能
 * 
 * ENABLE_* : 编译时开关，false 时相关代码不编译（节省资源）
 * REALTIME_*: 运行时模式：
 *   - true  = 实时打印每条记录
 *   - false = 仅维护环形缓冲区，出错时打印
 ***************************************************************************************/

#ifndef __CONFIG_H__
#define __CONFIG_H__

// ============ 设定最大仿真数 ============
#define DEFAULT_MAX_CYCLES 50000000

// ============ DIFFTEST (差分测试) ============
// 对比 npc2 (DUT) 和 nemu (REF) 的执行结果
// 需要先编译 nemu 为动态库：cd nemu && make menuconfig (选择 Shared object) && make
#define ENABLE_DIFFTEST false  // 差分开关

// REF 动态库路径（相对于运行目录或绝对路径）
// 默认使用 ysyx-workbench/nemu/build/riscv32-nemu-interpreter-so
// 如果在 am-kernels 目录下运行，需要调整为正确的相对路径
#define DIFFTEST_REF_PATH "/home/azazel/ysyx-workbench/nemu/build/riscv32-nemu-interpreter-so"

// ============ ITRACE (指令追踪) ============
#define ENABLE_ITRACE   1  // 编译开关
#define REALTIME_ITRACE 0 // 实时打印模式

// ============ FTRACE (函数追踪) ============
#define ENABLE_FTRACE   0  // 编译开关
#define REALTIME_FTRACE 0 // 实时打印模式

// ============ MTRACE (内存追踪) ============
#define ENABLE_MTRACE   0  // 编译开关
#define REALTIME_MTRACE 0 // 实时打印模式
#define MTRACE_RANGE_EN 0 // 是否仅追踪特定地址范围
#define MTRACE_START    0x80000000 // 过滤起始地址
#define MTRACE_END      0x80001000 // 过滤结束地址

// ============ DTRACE (设备访问追踪) ============
#define ENABLE_DTRACE   0  // 编译开关

// ============ SDB (Simple Debugger) ============
// true: 启动时进入交互式调试, false: 批处理模式直接运行
#define ENABLE_SDB      0

// ============ WAVEFORM (波形生成) ============
// 生成 wave.vcd，可用 gtkwave 查看
#define ENABLE_WAVEFORM 0

#endif /* __CONFIG_H__ */
