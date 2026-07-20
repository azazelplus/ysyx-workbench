/***************************************************************************************
 * NEMU 公共头文件 - 定义基本类型和宏
 * 
 * 这个文件定义了 NEMU 中使用的基本数据类型和格式化宏，
 * 使代码能够灵活支持不同的 ISA（指令集架构）和配置.
 * 
 * 主要内容
 * 1. 基本类型定义：word_t, vaddr_t, paddr_t 等
 * 2. 格式化宏：FMT_WORD, FMT_PADDR（用于 printf 等函数）
 * 3. 条件编译配置：根据 CONFIG_ISA64 等选择合适的类型
 ***************************************************************************************/

#ifndef __COMMON_H__
#define __COMMON_H__

#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include <generated/autoconf.h>
#include <macro.h>

#ifdef CONFIG_TARGET_AM
#include <klib.h>
#else
#include <assert.h>
#include <stdlib.h>
#endif

/***************************************************************************************
 * 物理内存配置宏
 * 【CONFIG_MBASE】
 * 物理内存的基址（起始地址）
 * 【CONFIG_MSIZE】
 * 物理内存的大小（字节数）
 ***************************************************************************************/
 // PMEM64 用来判断物理地址是否需要64bit. 如果系统物理地址超过4GB, 则需要64位的物理地址类型.
#if CONFIG_MBASE + CONFIG_MSIZE > 0x100000000ul 
#define PMEM64 1
#endif


// word_t - 无符号整数. 在32bit系统中为uint32_t, 在64bit系统中为uint64_t.
typedef MUXDEF(CONFIG_ISA64, uint64_t, uint32_t) word_t;

//sword=signed word,有符号.
typedef MUXDEF(CONFIG_ISA64, int64_t, int32_t)  sword_t;

//FMT_WORD
#define FMT_WORD MUXDEF(CONFIG_ISA64, "0x%016" PRIx64, "0x%08" PRIx32)

typedef word_t vaddr_t;

//paddr_t: 物理地址类型. 32位无符号
typedef MUXDEF(PMEM64, uint64_t, uint32_t) paddr_t;

// FMT_PADDR: 物理地址格式化宏, 用例:
#define FMT_PADDR MUXDEF(PMEM64, "0x%016" PRIx64, "0x%08" PRIx32)   

// ioaddr_t: IO端口地址类型. 16位无符号. 只用于x86的端口映射.
typedef uint16_t ioaddr_t;

#include <debug.h>

#endif
