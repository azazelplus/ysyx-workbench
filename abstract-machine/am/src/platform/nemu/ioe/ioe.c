#include <am.h>
#include <klib-macros.h>

/***************************************************************************************
 * IOE (Input/Output Environment) - 输入/输出环境管理
 * 
 * 这个模块提供统一的 IO 接口，让上层应用程序通过统一的 API 访问各种硬件设备：
 * - 计时器（TIMER）  : 获取系统时间
 * - 输入设备（INPUT）: 读取键盘等输入
 * - 显示设备（GPU）  : 帧缓冲绘图
 * - 音频设备（AUDIO）: 播放声音
 * 等等...
 * 
 * 【设计模式】- 查找表（Lookup Table）
 * - 每个硬件设备对应一个唯一的寄存器号（0-127）
 * - lut[] 是一个大小为 128 的数组，存储每个寄存器号对应的处理函数指针
 * - ioe_read() 和 ioe_write() 通过寄存器号查表，调用相应的设备处理函数
 * 
 * 【设备列表】
 * AM_TIMER_CONFIG / AM_TIMER_UPTIME  -> timer.c 中的处理函数
 * AM_INPUT_KEYBRD                     -> input.c 中的处理函数
 * AM_GPU_CONFIG / AM_GPU_FBDRAW       -> gpu.c 中的处理函数
 * AM_AUDIO_CONFIG / AM_AUDIO_PLAY     -> audio.c 中的处理函数
 * ...
 ***************************************************************************************/

void __am_timer_init();
void __am_gpu_init();
void __am_audio_init();
void __am_input_keybrd(AM_INPUT_KEYBRD_T *);
void __am_timer_rtc(AM_TIMER_RTC_T *);
void __am_timer_uptime(AM_TIMER_UPTIME_T *);
void __am_gpu_config(AM_GPU_CONFIG_T *);
void __am_gpu_status(AM_GPU_STATUS_T *);
void __am_gpu_fbdraw(AM_GPU_FBDRAW_T *);
void __am_audio_config(AM_AUDIO_CONFIG_T *);
void __am_audio_ctrl(AM_AUDIO_CTRL_T *);
void __am_audio_status(AM_AUDIO_STATUS_T *);
void __am_audio_play(AM_AUDIO_PLAY_T *);
void __am_disk_config(AM_DISK_CONFIG_T *cfg);
void __am_disk_status(AM_DISK_STATUS_T *stat);
void __am_disk_blkio(AM_DISK_BLKIO_T *io);

// 配置函数 - 返回设备是否存在及其属性
static void __am_timer_config(AM_TIMER_CONFIG_T *cfg) { cfg->present = true; cfg->has_rtc = true; }
static void __am_input_config(AM_INPUT_CONFIG_T *cfg) { cfg->present = true;  }
static void __am_uart_config(AM_UART_CONFIG_T *cfg)   { cfg->present = false; }
static void __am_net_config (AM_NET_CONFIG_T *cfg)    { cfg->present = false; }


/**
 * 处理函数类型定义
 * 所有 IO 设备的处理函数都是 void (*)(void *) 的形式：
 * - 参数是指向数据结构的指针（如 AM_TIMER_UPTIME_T、AM_GPU_FBDRAW_T 等）
 * - 函数读取或写入该结构中的数据，与硬件交互
 **/
typedef void (*handler_t)(void *buf);


/**
 * 查找表 (LUT - Lookup Table)
 * 
 * 【工作原理】
 * - 数组下标是寄存器号（来自 amdev.h 中的 enum 定义）
 * - 数组元素是处理该寄存器的函数指针
 * - 例如：lut[AM_TIMER_UPTIME] 指向 __am_timer_uptime 函数
 * 
 * 【使用流程】
 * 用户代码调用 io_read(AM_TIMER_UPTIME, &uptime);
 *   -> ioe_read() 查表找到 lut[AM_TIMER_UPTIME]
 *   -> 调用 __am_timer_uptime(&uptime)
 *   -> 该函数执行 inl(RTC_ADDR) 等操作，获取硬件数据
 *   -> uptime 结构被填充，返回给用户
 **/
static void *lut[128] = {
  [AM_TIMER_CONFIG] = __am_timer_config,
  [AM_TIMER_RTC   ] = __am_timer_rtc,
  [AM_TIMER_UPTIME] = __am_timer_uptime,
  [AM_INPUT_CONFIG] = __am_input_config,
  [AM_INPUT_KEYBRD] = __am_input_keybrd,
  [AM_GPU_CONFIG  ] = __am_gpu_config,
  [AM_GPU_FBDRAW  ] = __am_gpu_fbdraw,
  [AM_GPU_STATUS  ] = __am_gpu_status,
  [AM_UART_CONFIG ] = __am_uart_config,
  [AM_AUDIO_CONFIG] = __am_audio_config,
  [AM_AUDIO_CTRL  ] = __am_audio_ctrl,
  [AM_AUDIO_STATUS] = __am_audio_status,
  [AM_AUDIO_PLAY  ] = __am_audio_play,
  [AM_DISK_CONFIG ] = __am_disk_config,
  [AM_DISK_STATUS ] = __am_disk_status,
  [AM_DISK_BLKIO  ] = __am_disk_blkio,
  [AM_NET_CONFIG  ] = __am_net_config,
};


/**
 * fail - 默认的错误处理函数
 * 
 * 当程序尝试访问未实现的寄存器时调用此函数。
 */
static void fail(void *buf) { panic("access nonexist register"); }

/**
 * ioe_init - 初始化 IOE 子系统
 * 
 * 【执行流程】
 * 1. 遍历查找表，将所有空的表项都设置为 fail() 函数
 *    这样，对未实现的寄存器的访问会触发 panic（程序崩溃）
 * 2. 调用各个设备的初始化函数：
 *    - __am_gpu_init()   : 初始化 GPU（清屏、设置分辨率等）
 *    - __am_timer_init() : 初始化计时器（目前为空函数）
 *    - __am_audio_init() : 初始化音频设备
 * 
 * 【为什么要初始化？】
 * - GPU 需要设置好缓冲区
 * - Timer 需要注册 MMIO 处理函数
 * - Audio 需要初始化音频缓冲区
 * 
 * 【返回值】
 * true = 初始化成功
 ***************************************************************************************/
bool ioe_init() {
  for (int i = 0; i < LENGTH(lut); i++)
    if (!lut[i]) lut[i] = fail;
  __am_gpu_init();
  __am_timer_init();
  __am_audio_init();
  return true;
}

/**
 * ioe_read - 从 IOE 设备读取数据
 * 
 * 【执行流程】
 * 1. 用寄存器号 reg 在查找表中查询，得到处理函数指针
 * 2. 将 buf 指针强制转换为 handler_t 类型并调用
 * 3. 该函数会填充 buf 指向的数据结构
 * 
 * 【使用示例】
 * AM_TIMER_UPTIME_T uptime;
 * ioe_read(AM_TIMER_UPTIME, &uptime);
 *   -> 查表得到 lut[AM_TIMER_UPTIME] = __am_timer_uptime
 *   -> 调用 __am_timer_uptime(&uptime)
 *   -> uptime.us 被填充为当前的系统启动时间
 * 
 * 【参数说明】
 * @param reg: 寄存器号（如 AM_TIMER_UPTIME、AM_INPUT_KEYBRD 等）
 * @param buf: 指向输出数据的指针（格式取决于 reg）
 **/
void ioe_read (int reg, void *buf) { ((handler_t)lut[reg])(buf); }


/**
 * ioe_write - 向 IOE 设备写入数据
 * 
 * 【执行流程】
 * 同 ioe_read，但通常用于向设备发送命令或配置。
 * 
 * 【使用示例】
 * AM_GPU_FBDRAW_T frame = { ... };  // 初始化绘图参数
 * ioe_write(AM_GPU_FBDRAW, &frame);
 *   -> 查表得到 lut[AM_GPU_FBDRAW] = __am_gpu_fbdraw
 *   -> 调用 __am_gpu_fbdraw(&frame)
 *   -> 该函数将帧缓冲数据写入 GPU，显示画面
 * 
 * 【参数说明】
 * @param reg: 寄存器号
 * @param buf: 指向输入数据的指针
 **/
void ioe_write(int reg, void *buf) { ((handler_t)lut[reg])(buf); }
