/*
timer的抽象接口层. 为上层应用提供统一的接口
  隐藏硬件细节（"先读高位再读低位"的操作细节）
  提供跨平台的 API __am_timer_uptime()
  上层代码只需要调用这个函数，不需要知道硬件怎么工作
*/
#include <am.h>
#include <nemu.h>


// 初始化计时器设备.
void __am_timer_init() {
}


// 获取系统启动后的微秒数. 输入为 `AM_TIMER_UPTIME_T`结构体指针, 即amdev.h中定义的AM_TIMER_UPTIME_T结构体.
void __am_timer_uptime(AM_TIMER_UPTIME_T *uptime) {
  // *** 思路讲解 ***
  // 
  // 问: 为什么不能直接 uptime->us = 0; ?
  // 答: 这是题目给的 stub（占位符），提示这个函数需要正确实现。
  //     直接返回 0 没有任何意义 —— 无法完成"获取系统启动后的微秒数"这个功能。
  //
  // 实现思路:
  // 1. RTC 设备由 nemu/src/device/timer.c 模拟，提供两个 32 位寄存器:
  //    - RTC_ADDR + 0 (低 32 位): 时间的低 32 位
  //    - RTC_ADDR + 4 (高 32 位): 时间的高 32 位
  //    nemu.h 中定义: RTC_ADDR = DEVICE_BASE + 0x48 = 0xa0000048
  //
  // 2. 硬件工作方式 (rtc_io_handler 的逻辑):
  //    - 只有读取 offset=4（高 32 位）时，才会触发 get_time() 获取最新时间
  //    - 读取完高 32 位后，硬件会把时间值分别存入两个寄存器
  //    - 再读取 offset=0（低 32 位）时，读到的是刚刚更新的低 32 位值
  //
  // 3. 为什么要 "先读高位，后读低位"？
  //    因为必须确保两个 32 位值来自同一个时间快照。
  //    如果反过来（先读低位后读高位），可能会读到：
  //      低32位 = 时刻 T 的值  (因为T时刻还没更新)
  //      高32位 = 时刻 T+ΔT 的值 (因为这时才触发了更新)
  //    这样得到的 64 位值就是错的。
  //
  // 4. inl() 宏（在 riscv.h 中定义）:
  //    #define inl(addr) (*(volatile uint32_t *)(addr))
  //    用来从内存地址读取 32 位数据。

  uint32_t high = inl(RTC_ADDR + 4);  // 先读高 32 位，触发硬件获取当前时间
  uint32_t low  = inl(RTC_ADDR);       // 再读低 32 位，得到刚更新的值

  // 合并成 64 位时间戳 (单位: 微秒)
  uptime->us = (uint64_t)low + ((uint64_t)high << 32);
}

void __am_timer_rtc(AM_TIMER_RTC_T *rtc) {
  rtc->second = 0;
  rtc->minute = 0;
  rtc->hour   = 0;
  rtc->day    = 0;
  rtc->month  = 0;
  rtc->year   = 1900;
}
