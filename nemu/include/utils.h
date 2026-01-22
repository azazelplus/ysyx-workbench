/***************************************************************************************
* Copyright (c) 2014-2022 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
通用工具头文件. 它包含很多通用的宏工具, 如:

***************************************************************************************/

#ifndef __UTILS_H__
#define __UTILS_H__

//clangd的静态分析在这里会报错: 文件被递归包含. 无需考虑, 因为有include guard保护. 静态分析有时候处理的不完整.
#include <common.h>

// ----------- state -----------
//nemu模拟器的运行状态枚举类型.
enum { 
  NEMU_RUNNING,  // 暂停（还没运行或被暂停）
  NEMU_STOP, // 正在运行中
  NEMU_END, // 程序执行结束（正常停止）
  NEMU_ABORT, // 程序执行异常终止（错误或断言）
  NEMU_QUIT // 用户请求退出模拟器
};

typedef struct {
  int state;        //其值为上述枚举状态类型
  vaddr_t halt_pc;  //程序PC的值，在程序停止(trap / ebreak / 错误)时保存.
  uint32_t halt_ret;//程序的返回值，在程序停止时保存.
} NEMUState;        

extern NEMUState nemu_state;

// ----------- timer -----------

uint64_t get_time();

// ----------- log -----------

#define ANSI_FG_BLACK   "\33[1;30m"
#define ANSI_FG_RED     "\33[1;31m"
#define ANSI_FG_GREEN   "\33[1;32m"
#define ANSI_FG_YELLOW  "\33[1;33m"
#define ANSI_FG_BLUE    "\33[1;34m"
#define ANSI_FG_MAGENTA "\33[1;35m"
#define ANSI_FG_CYAN    "\33[1;36m"
#define ANSI_FG_WHITE   "\33[1;37m"
#define ANSI_BG_BLACK   "\33[1;40m"
#define ANSI_BG_RED     "\33[1;41m"
#define ANSI_BG_GREEN   "\33[1;42m"
#define ANSI_BG_YELLOW  "\33[1;43m"
#define ANSI_BG_BLUE    "\33[1;44m"
#define ANSI_BG_MAGENTA "\33[1;35m"
#define ANSI_BG_CYAN    "\33[1;46m"
#define ANSI_BG_WHITE   "\33[1;47m"
#define ANSI_NONE       "\33[0m"

#define ANSI_FMT(str, fmt) fmt str ANSI_NONE


// ============================================================================
// 日志系统
// ============================================================================

// log_write(...)宏 - 写入日志文件到日志文件指针log_fp.
// 用途：记录详细的运行日志到文件（如 instruction trace、memory trace）
// 示例：
//   log_write("PC=0x%x, instr=%s\n", cpu.pc, asm_str);
//   只有在 log_enable() 返回 true 时才会真正写入.  log_enable()宏检测当前指令要不要记录.
#define log_write(...) IFDEF(CONFIG_TARGET_NATIVE_ELF, \
  do { \
    extern FILE* log_fp; \
    extern bool log_enable(); \
    if (log_enable()) { \
      fprintf(log_fp, __VA_ARGS__); \
      fflush(log_fp); \
    } \
  } while (0) \
)


// _Log(...) - 既打印到终端，也写入日志文件
// 用途：重要信息既要让用户看到（stdout），也要记录到文件
// 示例：
//   _Log("NEMU started, PC=0x%x\n", cpu.pc);
//   会同时执行 printf(...) 和 log_write(...)
#define _Log(...) \
  do { \
    printf(__VA_ARGS__); \
    log_write(__VA_ARGS__); \
  } while (0)


#endif
