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
***************************************************************************************/

#include <device/map.h>
#include <memory/paddr.h>

// NR=number of registered mmio devices, 支持mmio外设数量为16个
// 例如：串口 1 个、RTC 1 个、GPU 1 个...等最多 16 个设备
#define NR_MAP 16

// 全局MMIO 设备数组. 它在 init_map() 中被初始化.
static IOMap maps[NR_MAP] = {};

// number of map, 已注册的 MMIO 设备数量. 就是当前maps数组占用数.
static int nr_map = 0;


/***************************************************************************************
 * fetch_mmio_map - 根据物理地址查找对应的 MMIO 设备映射
 * 
 * 【执行流程】
 * 1. 调用 find_mapid_by_addr() 在全局 MMIO 设备列表中搜索
 *    传入参数：
 *    - maps: 全局 MMIO 设备数组
 *    - nr_map: 已注册的设备数量（搜索范围是 maps[0..nr_map-1]）
 *    - addr: 要查找的物理地址
 * 2. 如果找到（mapid != -1），返回对应的 IOMap 结构体指针
 * 3. 如果未找到（mapid == -1），返回 NULL（表示地址未映射到任何设备）
 * 
 * 【使用示例】
 * CPU 执行 inl(0xa0000048) 读取 RTC 时：
 *   addr = 0xa0000048
 *   find_mapid_by_addr(maps, nr_map, addr) 搜索 nr_map 个设备
 *   找到 maps[i]，其中 maps[i].low=0xa0000048, maps[i].high=0xa000004f
 *   返回 &maps[i]，后续通过这个指针访问设备的内存和回调函数
 *
 * 【参数说明】
 * @param addr: 要查找的物理地址
 ***************************************************************************************/
static IOMap* fetch_mmio_map(paddr_t addr) {
  int mapid = find_mapid_by_addr(maps, nr_map, addr); //调用 find_mapid_by_addr() 在全局 MMIO 设备列表中搜索
  return (mapid == -1 ? NULL : &maps[mapid]); //
}


/***************************************************************************************
 * report_mmio_overlap - 报告 MMIO 地址范围重叠错误
 * 【执行流程】
 * 输出一条 panic 信息，显示两个地址范围重叠的设备名称和具体地址范围，然后程序崩溃。
 * 
 * 【使用场景】
 * 当试图注册第二个 RTC 设备，地址也是 0xa0000048~0xa000004f 时：
 *   新设备: "rtc2" @ [0xa0000048, 0xa000004f]
 *   已有设备: "rtc"  @ [0xa0000048, 0xa000004f]
 *   调用 report_mmio_overlap("rtc2", 0xa0000048, 0xa000004f, 
 *                            "rtc",  0xa0000048, 0xa000004f)
 *   输出: MMIO region rtc2@[0xa0000048, 0xa000004f] is overlapped 
 *        with rtc@[0xa0000048, 0xa000004f]
 * 
 * 【参数说明】
 * @param name1, l1, r1: 新设备的名称、起始地址、结束地址
 * @param name2, l2, r2: 已有设备的名称、起始地址、结束地址
 ***************************************************************************************/
static void report_mmio_overlap(const char *name1, paddr_t l1, paddr_t r1,
    const char *name2, paddr_t l2, paddr_t r2) {
  panic("MMIO region %s@[" FMT_PADDR ", " FMT_PADDR "] is overlapped "
               "with %s@[" FMT_PADDR ", " FMT_PADDR "]", name1, l1, r1, name2, l2, r2);
}


/***************************************************************************************
 * add_mmio_map - 注册一个 MMIO 设备.
 * 【参数说明】
 * @param name:     要新加入的 设备名称（如 "rtc"、"serial"、"gpu" 等）
 * @param addr:     设备的物理地址（MMIO 基址）
 * @param space:    设备寄存器的内存空间指针（由 new_space() 分配）
 * @param len:      设备占用的地址范围长度（字节数）
 * @param callback: 设备的 IO 处理函数（当 CPU 访问该地址时被调用）
 * 
 * 【使用示例】
 * // 初始化 RTC 设备，占用 8 字节空间，基址 0xa0000048
 * uint32_t *rtc_regs = (uint32_t *)new_space(8);
 * add_mmio_map("rtc", 0xa0000048, rtc_regs, 8, rtc_io_handler);
 * 
 * // 初始化串口设备，占用 8 字节空间，基址 0xa00003f8
 * uint32_t *uart_regs = (uint32_t *)new_space(8);
 * add_mmio_map("serial", 0xa00003f8, uart_regs, 8, uart_io_handler);
 ***************************************************************************************/
void add_mmio_map(const char *name, paddr_t addr, void *space, uint32_t len, io_callback_t callback) {
  assert(nr_map < NR_MAP);  //检查外设列表(全局数组maps, 最多16个外设)是否有足够的空间存储新设备（nr_map < NR_MAP）
  paddr_t left = addr, right = addr + len - 1;  //left和right是要加入的设备的地址范围.
  //如果地址范围在物理内存地址内, 说明地址重叠了(外设地址只能在规定的MMIO地址范围内.), 触发report_mmio_overlap()来报错. 
  if (in_pmem(left) || in_pmem(right)) {
    report_mmio_overlap(name, left, right, "pmem", PMEM_LEFT, PMEM_RIGHT);
  }

  //检查新设备的地址范围是否与已有的其他 MMIO 设备重叠
  for (int i = 0; i < nr_map; i++) {
    if (left <= maps[i].high && right >= maps[i].low) {
      report_mmio_overlap(name, left, right, maps[i].name, maps[i].low, maps[i].high);
    }
  }
  
  //将新设备的信息存入全局数组 maps[nr_map], 即完成注册.
  maps[nr_map] = (IOMap){ .name = name, .low = addr, .high = addr + len - 1,
    .space = space, .callback = callback };
  Log("Add mmio map '%s' at [" FMT_PADDR ", " FMT_PADDR "]",
      maps[nr_map].name, maps[nr_map].low, maps[nr_map].high);
  //  全局外设计数器+1
  nr_map ++;
}

/***************************************************************************************
 * mmio_read - 从 MMIO 地址读取数据（总线接口）
 * 
 * 【执行流程】
 * 1. 调用 fetch_mmio_map(addr) 找到地址对应的设备
 *    - 在全局 MMIO 设备列表中搜索，看哪个设备拥有这个地址
 *    - 返回对应的 IOMap 指针（如果找不到返回 NULL）
 * 2. 调用 map_read() 执行实际的读操作（map.c 中定义）
 *    - 检查地址边界
 *    - 调用设备的回调函数（产生硬件副作用）
 *    - 从内存中读取数据
 *    - 返回数据
 * 
 * 【完整调用链】
 * CPU 执行: inl(0xa0000048)  // 读取 RTC 高 32 位
 *   ↓
 * 内存系统: paddr_read(0xa0000048, 4)
 *   ↓
 * mmio_read(0xa0000048, 4)  ← 【这里】
 *   ↓
 * fetch_mmio_map(0xa0000048)
 *   ↓ 找到 RTC 设备对应的 IOMap
 * map_read(0xa0000048, 4, &maps[RTC_INDEX])
 *   ↓
 * 调用 rtc_io_handler(offset=4, len=4, is_write=false)
 *   ↓
 * 硬件副作用：get_time() 更新时间值
 *   ↓
 * 从 rtc_port_base + 4 读取高 32 位
 *   ↓
 * 返回时间值给 CPU
 * 
 * 【参数说明】
 * @param addr: 物理地址
 * @param len:  读取长度（字节数，通常 1、2、4 或 8）
 * 
 * 【返回值】
 * 读到的数据（word_t 类型，64 位整数）
 ***************************************************************************************/
word_t mmio_read(paddr_t addr, int len) {
  return map_read(addr, len, fetch_mmio_map(addr));
}

/***************************************************************************************
 * mmio_write - 向 MMIO 地址写入数据（总线接口）
 * 
 * 【执行流程】
 * 同 mmio_read()，但方向是写而不是读：
 * 1. 调用 fetch_mmio_map(addr) 找到地址对应的设备
 * 2. 调用 map_write() 执行实际的写操作（map.c 中定义）
 *    - 检查地址边界
 *    - 将数据写入设备的内存空间
 *    - 调用设备的回调函数（产生硬件副作用）
 * 
 * 【完整调用链】
 * CPU 执行: outb(SERIAL_PORT, 'A')  // 向串口写字符
 *   ↓
 * 内存系统: paddr_write(0xa00003f8, 1, 0x41)
 *   ↓
 * mmio_write(0xa00003f8, 1, 0x41)  ← 【这里】
 *   ↓
 * fetch_mmio_map(0xa00003f8)
 *   ↓ 找到串口设备对应的 IOMap
 * map_write(0xa00003f8, 1, 0x41, &maps[UART_INDEX])
 *   ↓
 * 将 0x41 写入 uart_regs[0]
 *   ↓
 * 调用 uart_io_handler(offset=0, len=1, is_write=true)
 *   ↓
 * 硬件副作用：把字符 'A' 输出到终端
 * 
 * 【参数说明】
 * @param addr: 物理地址
 * @param len:  写入长度（字节数）
 * @param data: 要写入的数据
 ***************************************************************************************/
void mmio_write(paddr_t addr, int len, word_t data) {
  map_write(addr, len, data, fetch_mmio_map(addr));
}
