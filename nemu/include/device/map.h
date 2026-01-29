/***************************************************************************************
定义了结构体类型IOMap. 
它描述一个映射的名字, 起始地址, 结束地址, 映射的目标空间, 回调函数.
nemu/src.device/io/map.c实现映射管理.
***************************************************************************************/

#ifndef __DEVICE_MAP_H__
#define __DEVICE_MAP_H__

#include <cpu/difftest.h>

// 定义数据类型: io_callback_t, 它是一个函数指针类型, 等价于: void (*)(uint32_t, int, bool)
// 然后你就可以:
  /*
  void io_handler(uint32_t addr, int len, bool is_write){
    //...
  }
  io_callback_t cb = io_handler;
  */
typedef void(*io_callback_t)(uint32_t, int, bool);

// 定义在map.c
uint8_t* new_space(int size);

// 每个外设都有一个IOMap结构体实例来描述它的MMIO映射.
typedef struct {
  //名字
  const char *name;
  // we treat ioaddr_t as paddr_t here
  paddr_t low;  //起始地址
  paddr_t high; //结束地址
  void *space;        //指向
  io_callback_t callback; //一个类型是`void (*)(uint32_t, int, bool)`的回调函数
} IOMap;


// 检查addr是否在map的地址范围内.
static inline bool map_inside(IOMap *map, paddr_t addr) {
  return (addr >= map->low && addr <= map->high);
}


/***************************************************************************************
 * find_mapid_by_addr - 在 MMIO 设备列表中查找给定addr地址所属哪个mmio设备的内存空间.
 * 
 * 【执行流程】
 * 1. 遍历设备数组 maps[0..size-1]
 * 2. 对于每个设备，检查地址 addr 是否在该设备的范围内
 *    使用 map_inside() 检查：addr >= maps[i].low && addr <= maps[i].high
 * 3. 如果找到，调用 difftest_skip_ref() 通知差分测试系统跳过该指令的状态对比
 *    （原因：NEMU 的设备行为与 QEMU/SPIKE 等参考模型略有不同）
 * 4. 返回找到的设备索引 i
 * 5. 如果遍历完所有设备都没找到，返回 -1
 * 
 * 【参数说明】
 * @param maps: MMIO 设备数组指针（指向 mmio.c 中的全局 maps[] 数组）
 * @param size: 要搜索的设备数量. 直接传入 mmio.c 中的全局变量 nr_map 即可.
 * @param addr: 要查找的物理地址
 * 
 * 【返回值】
 * i (0..size-1): IO设备在maps中的索引
 * -1:            未找到（地址未被任何设备占用）
 ***************************************************************************************/
static inline int find_mapid_by_addr(IOMap *maps, int size, paddr_t addr) {
  int i;
  // 遍历前 size 个设备，查找地址所属的设备
  for (i = 0; i < size; i ++) {
    //如果addr在第i个设备的地址范围内
    if (map_inside(maps + i, addr)) {
      difftest_skip_ref();  // 通知差分测试：跳过该指令的状态对比(为什麽?)
      return i; //返回设备的索引号
    }
  }
  return -1;  // 没有设备占用这个地址
}


void add_pio_map(const char *name, ioaddr_t addr,
        void *space, uint32_t len, io_callback_t callback);
void add_mmio_map(const char *name, paddr_t addr,
        void *space, uint32_t len, io_callback_t callback);

word_t map_read(paddr_t addr, int len, IOMap *map);
void map_write(paddr_t addr, int len, word_t data, IOMap *map);

#endif
