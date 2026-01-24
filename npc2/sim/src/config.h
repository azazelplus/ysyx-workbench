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

// ============ ITRACE (指令追踪) ============
#define ENABLE_ITRACE   true  // 编译开关
#define REALTIME_ITRACE false // 实时打印模式

// ============ FTRACE (函数追踪) ============
#define ENABLE_FTRACE   true  // 编译开关
#define REALTIME_FTRACE false // 实时打印模式

// ============ MTRACE (内存追踪) ============
#define ENABLE_MTRACE   true  // 编译开关
#define REALTIME_MTRACE false // 实时打印模式
#define MTRACE_RANGE_EN false // 是否仅追踪特定地址范围
#define MTRACE_START    0x80000000 // 过滤起始地址
#define MTRACE_END      0x80001000 // 过滤结束地址

#endif /* __CONFIG_H__ */
