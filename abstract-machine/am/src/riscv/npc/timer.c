#include <am.h>

// RTC 时钟地址（与 NEMU 保持一致）
#define RTC_ADDR 0xa0000048UL

void __am_timer_init() {
}

/***************************************************************************************
 * __am_timer_uptime - 获取系统启动后经过的时间
 * 
 * @param uptime: 输出参数，填充系统启动后的微秒数
 * 
 * 【说明】
 * 从 RTC_ADDR 读取 64 位时间戳（低 32 位在 offset 0，高 32 位在 offset 4）
 ***************************************************************************************/
void __am_timer_uptime(AM_TIMER_UPTIME_T *uptime) {
  // 读取 64 位时间：低 32 位 + 高 32 位
  uint32_t lo = *(volatile uint32_t *)RTC_ADDR;
  uint32_t hi = *(volatile uint32_t *)(RTC_ADDR + 4);
  uptime->us = ((uint64_t)hi << 32) | lo;
}

/***************************************************************************************
 * __am_timer_rtc - 获取当前日期时间（未实现）
 * 
 * @param rtc: 输出参数，填充年月日时分秒
 * 
 * 【说明】
 * NPC2 仿真环境暂不支持 RTC 日历功能，返回固定值
 ***************************************************************************************/
void __am_timer_rtc(AM_TIMER_RTC_T *rtc) {
  rtc->second = 0;
  rtc->minute = 0;
  rtc->hour   = 0;
  rtc->day    = 0;
  rtc->month  = 0;
  rtc->year   = 1900;
}
