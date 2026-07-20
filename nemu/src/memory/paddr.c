/***************************************************************************************
# 实现内存相关.

nemu的内存组成:

0x00000000-0x7FFFFFFF: 无意义. 在地址访问中直接报错. 
0x80000000-0x87FFFFFF: 物理内存. 对应宿主侧uint8_t类型的pmem[]数组. 一个大小为CONFIG_MSIZE的uint8_t数组. 分配在宿主linux机的BSS/DATA段. 典型配置为CONFIG_MSIZE=128MB. 
0x88000000-0x9FFFFFFF: 无意义. 在地址访问中直接报错.
0xA0000000-0xA130FFFF: MMIO设备空间. 典型配置2MB. 对应宿主侧:[src/device/io/map.c中的io_space指针所指向的2MB堆空间. 使用预分配大块内存 + 线性分配器(new_space). 这个线性分配只是移动指针, 是逻辑意义的分配, 实际上nemu一开始就一次性在宿主侧malloc了2MB堆空间(init_map()函数). 这个设计可以学到堆内存管理的一个技巧：预分配大块内存 + 线性分配器，比频繁 malloc/free 更高效!
  - 各个设备通过new_space()从这2MB中逐个分配. 目前是这样:
    • 0xa00003f8-0xa00003ff: SERIAL (8字节)
    • 0xa0000048-0xa000004f: RTC (8字节)
    • 0xa0000060-0xa0000063: KEYBOARD (4字节)
    • 0xa0000100-0xa0000107: VGACTL (8字节)
    • 0xa0000200-0xa0000217: AUDIO (24字节)
    • 0xa0000300-0xa000037f: DISK (~128字节)
    • 0xa1000000-0xa10752ff: VMEM/显存 (~1.9MB)
    • 0xa1200000-0xa120ffff: 音频缓冲 (64KB)


#
# nemu 虚拟 CPU 看到的地址空间（以 RISCV 为例）:
# ┌─────────────────────────────────────────────────────────────┐
# │ 0x00000000                                                  │
# │    ...                                                      │
# │ 0x7FFFFFFF                                                  │
# ├─────────────────────────────────────────────────────────────┤ ← 用户程序通常在此
# │ 0x80000000  ─┐                                              │
# │    ...       │  pmem[] 数组 (CONFIG_MBASE ~ +CONFIG_MSIZE)  │
# │    ...       │  (典型: 128 MB, 0x80000000 ~ 0x87FFFFFF)      │
# │ 0x87FFFFFF  ─┘                                              │
# ├─────────────────────────────────────────────────────────────┤ ← MMIO 设备空间
# │ 0xa0000000  ─┐  DEVICE_BASE                                 │
# │   ...        │  • 0xa00003f8: SERIAL (8 字节)               │
# │ 0xa0000048   │  • 0xa0000048: RTC (8 字节)                  │
# │ 0xa0000060   │  • 0xa0000060: KEYBOARD (4 字节)             │
# │ 0xa0000100   │  • 0xa0000100: VGACTL (8 字节)               │
# │   ...        │    - offset 0: SIZE 寄存器 (width<<16|height) │
# │ 0xa0000104   │    - offset 4: SYNC 寄存器 (刷新信号)        │
# │ 0xa0000200   │  • 0xa0000200: AUDIO (24 字节)               │
# │ 0xa0000300   │  • 0xa0000300: DISK (128 字节)               │
# │            ─┘                                              │
# ├─────────────────────────────────────────────────────────────┤ ← 帧缓冲空间
# │ 0xa1000000  ─┐  MMIO_BASE + 0x1000000                       │
# │    ...       │  vmem (显存/Frame Buffer)                     │
# │    ...       │  典型容量: 800×600×4 = 1920000 字节          │
# │ 0xa10752ff  ─┘  (0xa1000000 ~ 0xa10752ff)                   │
# │                                                             │
# │ 0xa1200000  ─┐  音频缓冲区                                  │
# │    ...       │  (64KB)                                      │
# │ 0xa120ffff  ─┘                                              │
# └─────────────────────────────────────────────────────────────┘
#
# 【重点说明】
# • pmem[] 和 MMIO 在虚拟CPU看到的地址空间中完全分开
# • pmem 范围: [0x80000000, 0x87FFFFFF] (CONFIG_MBASE 开始, 128MB)
# • MMIO 范围: [0xa0000000, 0xa1300000) (DEVICE_BASE 开始)
# • CPU 访问时：if (addr in pmem) → 访问 pmem[] 数组
#              else → 查 MMIO 映射表 → 访问 io_space 中分配的设备内存
#
# 实现MTRACE_LOG宏. 它利用了utils.h
***************************************************************************************/

#include <memory/host.h>
#include <memory/paddr.h>
#include <device/mmio.h>
#include <isa.h>

// ============ MTRACE 配置 ============
// 如果在Kconfig中打开了MTRACE选项, 则CONFIG_MTRACE=1
#ifdef CONFIG_MTRACE
  #ifdef CONFIG_MTRACE_RANGE_ENABLE
    //定义一个 检查宏MTRACE_IN_RANGE(addr), 如果传入的addr在用户设置的[START, END)范围内则返回true
    #define MTRACE_IN_RANGE(addr) \
      ((addr) >= CONFIG_MTRACE_RANGE_START && (addr) < CONFIG_MTRACE_RANGE_END)
  #else
  // 如果没有开启地址范围过滤MTRACE_RANGE_ENABLE, 则所有地址都认为在范围内, 返回true
    #define MTRACE_IN_RANGE(addr) true
  #endif

  // 日志宏.
  #define MTRACE_LOG(type, addr, len, data) \
    do { \
      if (CONFIG_MTRACE_COND_EXPR && MTRACE_IN_RANGE(addr)) { \
        log_write("[mtrace] %s: addr=" FMT_PADDR ", len=%d, data=" FMT_WORD ", pc=" FMT_WORD "\n", \
                  type, addr, len, data, cpu.pc); \
      } \
    } while (0)
#else
  //如果没有开启MTRACE, 日志宏什麽也不做
  #define MTRACE_LOG(type, addr, len, data) ((void)0)
#endif



//分配nemu的物理内存数组 pmem. pmem数组的大小由CONFIG_MSIZE配置决定. 物理内存的起始地址由CONFIG_MBASE配置决定. 物理内存的范围是[CONFIG_MBASE, CONFIG_MBASE + CONFIG_MSIZE)左闭右开区间.
#if defined(CONFIG_PMEM_MALLOC)
static uint8_t *pmem = NULL;
#else // CONFIG_PMEM_GARRAY
static uint8_t pmem[CONFIG_MSIZE] PG_ALIGN = {};  
#endif



/* ============================================================================
 * 物理内存地址转换函数
 * ============================================================================
 * 
 * 【地址空间说明】
 * NEMU 物理内存是一个数组：uint8_t pmem[CONFIG_MSIZE]
 * - 数组索引：[0, CONFIG_MSIZE)
 * - 客户机物理地址空间：[CONFIG_MBASE, CONFIG_MBASE + CONFIG_MSIZE)
 * 
 * 典型配置：CONFIG_MBASE = 0x80000000, CONFIG_MSIZE = 128MB
 * 
 * 转换关系：
 *   pmem 数组                    客户机物理地址
 *   [0]                    ←→    0x80000000 (CONFIG_MBASE)
 *   [1]                    ←→    0x80000001
 *   [4]                    ←→    0x80000004
 *   ...
 *   [127MB-1]              ←→    0x87FFFFFF
 * ============================================================================ */

/**
 * guest_to_host - 客户机物理地址 → 主机内存指针
 * @paddr: 客户机物理地址（如 0x80000004）
 * @return: 指向 pmem 数组中对应位置的指针
 * 
 * 公式：pmem_index = paddr - CONFIG_MBASE
 * 例如：paddr=0x80000004, CONFIG_MBASE=0x80000000 → pmem[4]
 */
uint8_t* guest_to_host(paddr_t paddr) { 
    return pmem + paddr - CONFIG_MBASE; 
}

/**
 * host_to_guest - 主机内存指针 → 客户机物理地址
 * @haddr: 指向 pmem 数组某位置的指针
 * @return: 对应的客户机物理地址
 * 
 * 公式：paddr = (haddr - pmem) + CONFIG_MBASE
 * 这是 guest_to_host 的反向操作
 */
paddr_t host_to_guest(uint8_t *haddr) { 
    return haddr - pmem + CONFIG_MBASE; 
}

/**
 * pmem_read - 底层物理内存读取
 * @addr: 客户机物理地址
 * @len: 读取字节数 (1, 2, 4)
 * @return: 读取的数据（高位补 0）
 * 
 * 功能：
 * 1. 将客户机地址转换为 pmem 数组索引
 * 2. 调用 host_read() 进行实际内存读取
 * 
 * 例如：pmem_read(0x80000004, 4) → 从 pmem[4] 读取 4 字节数据
 */
static word_t pmem_read(paddr_t addr, int len) {
  word_t ret = host_read(guest_to_host(addr), len);
  return ret;
}

/**
 * pmem_write - 底层物理内存写入
 * @addr: 客户机物理地址
 * @len: 写入字节数 (1, 2, 4)
 * @data: 要写入的数据
 * 
 * 功能：
 * 1. 将客户机地址转换为 pmem 数组索引
 * 2. 调用 host_write() 进行实际内存写入
 * 
 * 例如：pmem_write(0x80000004, 4, 0x12345678) → 向 pmem[4] 写入 4 字节
 */
static void pmem_write(paddr_t addr, int len, word_t data) {
  host_write(guest_to_host(addr), len, data);
}

/**
 * out_of_bound - 访问越界错误处理
 * @addr: 非法的物理地址
 * 
 * 功能：
 * 如果地址既不在物理内存范围内，也不是有效的 MMIO 地址，
 * 则调用此函数报错并终止仿真
 * 
 * 打印信息包括：
 * - 非法地址
 * - 有效的物理内存范围 [PMEM_LEFT, PMEM_RIGHT)
 * - 出错时的 PC（用于调试）
 */
static void out_of_bound(paddr_t addr) {
  panic("address = " FMT_PADDR " is out of bound of pmem [" FMT_PADDR ", " FMT_PADDR "] at pc = " FMT_WORD,
      addr, PMEM_LEFT, PMEM_RIGHT, cpu.pc);
}

/* ============================================================================
 * init_mem - 初始化物理内存
 * 
 * 【初始化步骤】
 * 1. 内存分配:
 *    - CONFIG_PMEM_MALLOC开启时: 动态分配 (malloc)
 *    - CONFIG_PMEM_MALLOC开关闭: 什麽都不做.
 * 2. 随机初始化内存
 *    - CONFIG_MEM_RANDOM 开启时，用随机数填充内存
 *    - CONFIG_MEM_RANDOM 关闭时，什麽都不做.
 * 3. 打印内存范围信息供用户确认.
 * 是的, 如果配置是静态全局内粗+不需要随机初始化, init_mem() 函数实际上什么都不做.
 * ============================================================================ */
void init_mem() {
  // 如果配置为动态分配，从堆上分配内存
#if   defined(CONFIG_PMEM_MALLOC)
  pmem = malloc(CONFIG_MSIZE);
  assert(pmem);
#endif

  // 如果配置为随机初始化，用随机数填充所有内存
  // （IFDEF 宏：如果条件编译开启，执行第二个参数；否则什么都不做）
  IFDEF(CONFIG_MEM_RANDOM, memset(pmem, rand(), CONFIG_MSIZE));
  
  // 打印内存范围（用于确认配置是否正确）
  Log("physical memory area [" FMT_PADDR ", " FMT_PADDR "]", PMEM_LEFT, PMEM_RIGHT);
}

/**  ============================================================================
 * paddr_read - 读取物理内存（公共接口）
 * 
 * @param addr: 客户机物理地址（如 0x80000008）
 * @param len: 读取字节数（1, 2, 4）
 * @param return: 读取的数据（若 len<4，高位补 0）
 * 
 * 【访问流程】
 * 1. 检查地址是否在有效的物理内存范围内
 *    ✓ 是 → 调用 pmem_read() 读取，记录 mtrace，返回数据
 *    ✗ 否 → 尝试 MMIO 读取（如果启用了外设支持）
 *           最后若都失败 → 调用 out_of_bound() 报错
 * 
 * 【likely 宏说明】
 * likely(exp) 是性能优化提示, 告诉 CPU 分支预测器 exp 通常为真.
 * - likely(in_pmem(addr))表示大多数内存访问确实是物理内存，而不是 MMIO.
 * - 如果预测正确，避免流水线冲刷，提升性能
 * ============================================================================ */
word_t paddr_read(paddr_t addr, int len) {
  word_t data = 0;  // 初始化为 0（若地址越界或部分读取，高位会是 0）
  
  // 快速路径：大多数访问都是物理内存，用 likely 优化分支预测
  if (likely(in_pmem(addr))) {
    // 从物理内存读取
    data = pmem_read(addr, len);
    
    // 记录内存追踪日志（如果启用了 CONFIG_MTRACE）
    MTRACE_LOG("READ ", addr, len, data);
    
    return data;
  }
  
  // 慢速路径：可能是 MMIO 访问
  // IFDEF 宏：如果启用了 CONFIG_DEVICE，执行大括号内的代码
  IFDEF(CONFIG_DEVICE, 
    data = mmio_read(addr, len);  // 从 MMIO 地址读取
    // 记录 MMIO 访问日志
    MTRACE_LOG("MMIO_R", addr, len, data); 
    return data;
  );

  // 如果既不是物理内存也不是 MMIO，地址非法，报错
  out_of_bound(addr);
  return 0;
}

/* ============================================================================
 * paddr_write - 写入物理内存（公共接口）
 * 
 * @addr: 客户机物理地址
 * @len: 写入字节数（1, 2, 4）
 * @data: 要写入的数据
 * 
 * 【访问流程】
 * 1. 记录 MTRACE 日志（写操作发生）
 * 2. 检查地址是否在有效的物理内存范围内
 *    ✓ 是 → 调用 pmem_write() 写入，返回
 *    ✗ 否 → 尝试 MMIO 写入（如果启用了外设支持）
 *           最后若都失败 → 调用 out_of_bound() 报错
 * 
 * 【与 paddr_read 的区别】
 * - MTRACE_LOG 在前面记录（无论后续访问成功否）
 * - likely 优化同样存在
 * - MMIO 写入也会记录独立的日志标记（"MMIO_W"）
 * ============================================================================ */
void paddr_write(paddr_t addr, int len, word_t data) {
  // 先记录本次写操作的日志（如果启用了 CONFIG_MTRACE）
  MTRACE_LOG("WRITE", addr, len, data);
  
  // 快速路径：大多数访问都是物理内存
  if (likely(in_pmem(addr))) { 
    pmem_write(addr, len, data); 
    return; 
  }
  
  // 慢速路径：可能是 MMIO 访问
  IFDEF(CONFIG_DEVICE, 
    // 记录 MMIO 写操作日志
    MTRACE_LOG("MMIO_W", addr, len, data); 
    // 执行 MMIO 写入
    mmio_write(addr, len, data); 
    return;
  );

  // 如果既不是物理内存也不是 MMIO，地址非法，报错
  out_of_bound(addr);
}



