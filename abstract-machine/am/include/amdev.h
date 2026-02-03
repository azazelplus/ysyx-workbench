#ifndef __AMDEV_H__
#define __AMDEV_H__

// **MAY SUBJECT TO CHANGE IN THE FUTURE**
/***************************************************************************************
本头文件用AM_DEVREG宏, 枚举定义了AM的所有抽象设备寄存器(AM Device Registers).
实际上就是在全局空间用enmu和typedef struct定义了多组寄存器. 每组寄存器用两个全局变量描述:(reg是寄存器名) 
  * 一个叫做`AM_$(reg)_T`的结构体类型 (T表示Type), 结构体里的成员变量就是该寄存器的各个域(field).
  * 一个叫做`AM_$(reg)`的整数常量, 表达该reg的编号. 用enum
 * @param id: 编号数字.
 * @param reg: 期望是一个寄存器名称字符串, 会被##拼接.
 * @param ...: 期望得到类似于 `int field1; bool field2;` 这样的成员列表, 用于定义结构体.
perm=permission, 读写权限标志, 目前仅作标记用, 没有实际功能. RD=read, WR=write, WRITE=read/write
* 例如:
* AM_DEVREG( 5, TIMER_RTC,    RD, int year, month, day, hour, minute, second);
宏替换后, 生成: 
 * 一个整数AM_TIMER_RTC:   enmu{AM_TIMER_RTC = 5};(等价于 int AM_TIMER_RTC = 5; ) 
 * 一个结构体类型AM_TIMER_RTC_T: typedef struct { int year, month, day, hour, minute, second; } AM_TIMER_RTC_T;
*/
#define AM_DEVREG(id, reg, perm, ...) \
  enum { AM_##reg = (id) }; \
  typedef struct { __VA_ARGS__; } AM_##reg##_T;

AM_DEVREG( 1, UART_CONFIG,  RD, bool present);
AM_DEVREG( 2, UART_TX,      WR, char data);
AM_DEVREG( 3, UART_RX,      RD, char data);
AM_DEVREG( 4, TIMER_CONFIG, RD, bool present, has_rtc);
//AM_TIMER_RTC, 可读出当前的年月日时分秒. PA中暂不使用.
AM_DEVREG( 5, TIMER_RTC,    RD, int year, month, day, hour, minute, second);
//AM_TIMER_UPTIME, AM系统启动时间, 可读出系统启动后的微秒数. 它也是一个RTC(时间流逝速率和真实时间一致), 只不过是从0开始数的
AM_DEVREG( 6, TIMER_UPTIME, RD, uint64_t us);
AM_DEVREG( 7, INPUT_CONFIG, RD, bool present);
AM_DEVREG( 8, INPUT_KEYBRD, RD, bool keydown; int keycode);


// bstract-machine/am/include/amdev.h中为GPU定义了五个抽象寄存器, 在NEMU中只会用到其中的两个:
AM_DEVREG( 9, GPU_CONFIG,   RD, bool present, has_accel; int width, height, vmemsz); // AM显示控制器信息, 可读出屏幕大小信息width和height. 另外AM假设系统在运行过程中, 屏幕大小不会发生变化.
AM_DEVREG(10, GPU_STATUS,   RD, bool ready);
AM_DEVREG(11, GPU_FBDRAW,   WR, int x, y; void *pixels; int w, h; bool sync); //  AM帧缓冲控制器, 可写入绘图信息, 向屏幕(x, y)坐标处绘制w*h的矩形图像. 图像像素按行优先方式存储在pixels中, 每个像素用32位整数以00RRGGBB的方式描述颜色. 若sync为true, 则马上将帧缓冲中的内容同步到屏幕上.
AM_DEVREG(12, GPU_MEMCPY,   WR, uint32_t dest; void *src; int size);
AM_DEVREG(13, GPU_RENDER,   WR, uint32_t root);


AM_DEVREG(14, AUDIO_CONFIG, RD, bool present; int bufsize);
AM_DEVREG(15, AUDIO_CTRL,   WR, int freq, channels, samples);
AM_DEVREG(16, AUDIO_STATUS, RD, int count);
AM_DEVREG(17, AUDIO_PLAY,   WR, Area buf);
AM_DEVREG(18, DISK_CONFIG,  RD, bool present; int blksz, blkcnt);
AM_DEVREG(19, DISK_STATUS,  RD, bool ready);
AM_DEVREG(20, DISK_BLKIO,   WR, bool write; void *buf; int blkno, blkcnt);
AM_DEVREG(21, NET_CONFIG,   RD, bool present);
AM_DEVREG(22, NET_STATUS,   RD, int rx_len, tx_len);
AM_DEVREG(23, NET_TX,       WR, Area buf);
AM_DEVREG(24, NET_RX,       WR, Area buf);

// Input

#define AM_KEYS(_) \
  _(ESCAPE) _(F1) _(F2) _(F3) _(F4) _(F5) _(F6) _(F7) _(F8) _(F9) _(F10) _(F11) _(F12) \
  _(GRAVE) _(1) _(2) _(3) _(4) _(5) _(6) _(7) _(8) _(9) _(0) _(MINUS) _(EQUALS) _(BACKSPACE) \
  _(TAB) _(Q) _(W) _(E) _(R) _(T) _(Y) _(U) _(I) _(O) _(P) _(LEFTBRACKET) _(RIGHTBRACKET) _(BACKSLASH) \
  _(CAPSLOCK) _(A) _(S) _(D) _(F) _(G) _(H) _(J) _(K) _(L) _(SEMICOLON) _(APOSTROPHE) _(RETURN) \
  _(LSHIFT) _(Z) _(X) _(C) _(V) _(B) _(N) _(M) _(COMMA) _(PERIOD) _(SLASH) _(RSHIFT) \
  _(LCTRL) _(APPLICATION) _(LALT) _(SPACE) _(RALT) _(RCTRL) \
  _(UP) _(DOWN) _(LEFT) _(RIGHT) _(INSERT) _(DELETE) _(HOME) _(END) _(PAGEUP) _(PAGEDOWN)

#define AM_KEY_NAMES(key) AM_KEY_##key,
enum {
  AM_KEY_NONE = 0,
  AM_KEYS(AM_KEY_NAMES)
};

// GPU

#define AM_GPU_TEXTURE  1
#define AM_GPU_SUBTREE  2
#define AM_GPU_NULL     0xffffffff

typedef uint32_t gpuptr_t;

struct gpu_texturedesc {
  uint16_t w, h;
  gpuptr_t pixels;
} __attribute__((packed));

struct gpu_canvas {
  uint16_t type, w, h, x1, y1, w1, h1;
  gpuptr_t sibling;
  union {
    gpuptr_t child;
    struct gpu_texturedesc texture;
  };
} __attribute__((packed));

#endif
