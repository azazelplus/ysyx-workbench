/***************************************************************************************
timer的硬件模拟层. 本质上就是宿主机的真实墙钟时间（相对 NEMU 启动时刻的偏移量），通过 MMIO 回调机制在 CPU 读取 RTC 寄存器时实时注入...
模拟真实硬件i8253计时器的功能:
  监听对特定地址（0xa0000048）的读写操作;
  当读取 offset=4 时，调用 get_time() 获取当前模拟时间;
  把时间值写到两个 32 位寄存器;

初始化时会分别注册0x48处长度为8个字节的端口, 以及0xa0000048处长度为8字节的MMIO空间, 它们都会映射到两个32位的RTC寄存器. CPU可以访问这两个寄存器来获得用64位表示的当前时间.

abstract-machine/am/include/amdev.h中为时钟的功能定义了两个抽象寄存器:
  AM_TIMER_RTC, AM实时时钟(RTC, Real Time Clock), 可读出当前的年月日时分秒. PA中暂不使用.
  AM_TIMER_UPTIME, AM系统启动时间, 可读出系统启动后的微秒数.

【调用链】
用户程序: ioe_read(AM_TIMER_UPTIME, &uptime);
  ↓ [ioe.c: ioe_read]
AM IOE层: __am_timer_uptime(&uptime);
  ↓ [timer.c (本文件)]
应用层: inl(RTC_ADDR + 4);  // 读取高32位
  ↓ [map.c: map_read]
NEMU模拟: map_read() -> invoke_callback() -> rtc_io_handler()
  ↓ [此处: rtc_io_handler]
硬件模拟: get_time() 获取当前时刻，更新寄存器
  ↓
应用层: inl(RTC_ADDR);      // 读取低32位
  ↓
返回 64 位时间戳给用户程序
***************************************************************************************/

#include <device/map.h>
#include <device/alarm.h>
#include <utils.h>

// RTC 寄存器基地址指针. 在 init_timer()函数中初始化, 分配两个元素:
//   rtc_port_base = (uint32_t *)new_space(8);  //指向 new_space(8) 分配的 8 字节内存
// rtc_port_base[0] = 时间的低 32 位. 映射到物理地址0xa0000048
// rtc_port_base[1] = 时间的高 32 位. 映射到物理地址0xa000004c
static uint32_t *rtc_port_base = NULL;


/***************************************************************************************
 * rtc_io_handler - RTC外设的回调函数.
 * 
 * 【何时被调用(如何实现通过访存rtc所映射的内存, 来触发调用, 从而触发行为)】
 * 当 CPU 执行 inl/inw/inb 等函数,访问 0xa0000048~0xa000004f 地址时,
 * nemu执行lw指令(在ISTPAT宏)时, 调用vaddr_read(), 当前没有开启虚拟内存时, vaddr_read()直接转发到paddr_read().  paddr_read()判断addr是mmio地址后, 调用mmio_read读取数据:
      data = mmio_read(addr, len);  // 从 MMIO 地址读取
 * 而mmio_read()直接转发到map_read():
      return map_read(addr, len, fetch_mmio_map(addr));
 * map_read()从 MMIO 地址读取数据, 同时调用设备的回调函数: 
 *    invoke_callback(map->callback, offset, len, false); // prepare data to read
 * invoke_callback()函数会调用rtc_io_handler(offset, len, is_write)函数.
 * 
 * 【参数说明】
 * @param offset:   相对于设备基址的偏移（0 或 4）
 *                  offset=0 = 低 32 位地址
 *                  offset=4 = 高 32 位地址
 * @param len:      访问长度（1、2 或 4 字节）
 * @param is_write: true = 写操作，false = 读操作
 ***************************************************************************************/
static void rtc_io_handler(uint32_t offset, int len, bool is_write) {
  assert(offset == 0 || offset == 4);
  // 只在读取高 32 位时更新时间值
  if (!is_write && offset == 4) {
    // NEMU 内部获取当前模拟时间（单位：微秒）
    uint64_t us = get_time();
    // 将 64 位时间拆成两个 32 位，存到设备寄存器中
    // 下一步当 CPU 读 offset=0 时，会读到这个更新后的低 32 位值
    rtc_port_base[0] = (uint32_t)us;       // 低 32 位
    rtc_port_base[1] = us >> 32;           // 高 32 位
  }
}

#ifndef CONFIG_TARGET_AM
static void timer_intr() {
  if (nemu_state.state == NEMU_RUNNING) {
    extern void dev_raise_intr();
    dev_raise_intr();
  }
}
#endif


/***************************************************************************************
 * init_timer - 初始化计时器设备. 给rtc_port_base指针分配2个元素(即8字节)的内存空间, 并注册到MMIO映射系统中.
 * 
 * NEMU 启动时，在 init_device() 中调用，用来初始化所有设备.
 * 
 * 【重要概念】
 * add_mmio_map() 将"物理地址范围"和"内存区域"绑定在一起：
 * - 物理地址 0xa0000048: 映射到 rtc_port_base[0] (低 32 位)
 * - 物理地址 0xa000004c: 映射到 rtc_port_base[1] (高 32 位)
 * 
 * CPU 执行 inl(0xa000004c) 时：
 *   -> NEMU 将 0xa000004c 地址转换为 rtc_port_base + 4
 *   -> 调用 add_mmio_map 时注册的回调函数 rtc_io_handler(offset=4, ...)
 *   -> rtc_io_handler 从 NEMU 获取时间，更新寄存器
 *   -> CPU 读到的是最新的高 32 位时间值
 ***************************************************************************************/
void init_timer() {
  // 分配 8 字节内存给 RTC 寄存器
  rtc_port_base = (uint32_t *)new_space(8);

  // 注册到 MMIO 映射系统.
#ifdef CONFIG_HAS_PORT_IO
  add_pio_map ("rtc", CONFIG_RTC_PORT, rtc_port_base, 8, rtc_io_handler);
#else
  add_mmio_map("rtc", CONFIG_RTC_MMIO, rtc_port_base, 8, rtc_io_handler); // add_mmio_map()绑定. 0xa0000048 -> rtc_port_base[0] (低 32 位); 0xa000004c->rtc_port_base[1] (高 32 位).
#endif
  IFNDEF(CONFIG_TARGET_AM, add_alarm_handle(timer_intr));
}
