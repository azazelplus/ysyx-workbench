/***************************************************************************************
nemu的vga硬件实现. 它是一块简陋的显卡.
它拥有的资源(注册时(或者说初始化时)会申请的MMIO地址):
从0xa1000000开始的Framebuffer空间, 大小为SCREEN_W x SCREEN_H x 4字节(32bit RGBA模式).
VGA 的状态寄存器完全就是 io_space 堆内存中的几个 uint32_t 值.

nemu VGA的硬件模拟由三个寄存器组成:
三个寄存器的宿主侧映射:
size: 4byte. 宽高信息.  信息: (width << 16) | height
sync: 4byte. 刷新屏幕的握手信号. 非0表示触发刷新.
vmem: size*4 = (当前典型800×600×4) =  1920 KB, 显存(帧缓冲数据). 每4byte是一个像素数据.

MMIO地址	 寄存器名	 宿主侧变量	              堆内存偏移
0xa0000100	size	vgactl_port_base[0]	  io_space + offset_0
0xa0000104	sync	vgactl_port_base[1]	  io_space + offset_4
0xa1000000	vmem	vmem 指针	             io_space + offset_N

屏幕刷新握手逻辑:

1. AM用户程序写入像素数据
   io_write(AM_GPU_FBDRAW, x, y, pixels, w, h, sync=true)
          ↓
   [abstract-machine/am/src/platform/nemu/ioe/gpu.c]
   __am_gpu_fbdraw()
        ├─ 逐像素写入 vmem[]
        └─ if (sync) outl(SYNC_ADDR, 1)  ← 写 vgactl_port_base[1] = 1

2. NEMU 定期检查 SYNC 寄存器
   device_update()  [device.c] 刷新所有外设状态. [device.c]. 它在exec_once中执行. 不过并不是每次都执行, device_update()函数内有限频逻辑, 每执行约 16k 条指令执行一次. ~60Hz.
        ↓ 调用
   vga_update_screen()  [src/device/vga.c]
        ├─ if (vgactl_port_base[1] != 0)  ← 检查握手信号
        │   ├─ update_screen()            ← 调用 SDL 刷新屏幕
        │   └─ vgactl_port_base[1] = 0    ← 清零握手信号
        └─ return



***************************************************************************************/

#include <common.h>
#include <device/map.h>

//分辨率选则: 如果Kconfig设置为800x600则为此, 否则默认为400x300
//CONFIG_VGA_SIZE_800x600宏来自Kconfig生成: Fevice-->分辨率选择
#define SCREEN_W (MUXDEF(CONFIG_VGA_SIZE_800x600, 800, 400))
#define SCREEN_H (MUXDEF(CONFIG_VGA_SIZE_800x600, 600, 300))


/*******************************VGA寄存器的全局指针*****************************************/
// vmem: 显存寄存器的内存指针。会在 init_vga() 中通过 new_space 分配(当前是800x600x4=1920KB)
static void *vmem = NULL;

// vgactl_port_base: VGA 控制寄存器(ctl=control)基址指针。
// 指向一块 8 字节的内存区域:
// - offset 0 (4 bytes): 屏幕尺寸信息 (高 16 位宽, 低 16 位高)
// - offset 4 (4 bytes): 同步 (SYNC) 寄存器 (非 0 表示触发刷新)
static uint32_t *vgactl_port_base = NULL;
/***************************************************************************************/




/***************************************************************************************
 * screen_width - 获取屏幕宽度
 * @return: 当前屏幕宽度 (像素). 一般来说, return语句直接返回`SCREEN_W`的值.
 ***************************************************************************************/
static uint32_t screen_width() {
  return MUXDEF(CONFIG_TARGET_AM, io_read(AM_GPU_CONFIG).width, SCREEN_W);
}

/***************************************************************************************
 * screen_height - 获取屏幕高度
 * 同上.
 ***************************************************************************************/
static uint32_t screen_height() {
  return MUXDEF(CONFIG_TARGET_AM, io_read(AM_GPU_CONFIG).height, SCREEN_H);
}


/***************************************************************************************
 * screen_size - 获取显存总大小 (字节)
 * @return: width * height * 4 (即 32-bit RGBA 模式下的总字节数)。
 ***************************************************************************************/
static uint32_t screen_size() {
  return screen_width() * screen_height() * sizeof(uint32_t);
}




// `CONFIG_VGA_SHOW_SCREEN`宏来自kconfig的Devices-->Enable VGA-->Enable VGA Screen Display选项. 如果启用该选项, 则在宿主机上显示SDL窗口来显示vga内容.
#ifdef CONFIG_VGA_SHOW_SCREEN
#ifndef CONFIG_TARGET_AM
#include <SDL2/SDL.h>

static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;

/***************************************************************************************
 * init_screen - 初始化 SDL 窗口 (仅宿主为 Linux/Windows 时)
 * 
 * 【功能】
 * 调用 SDL 库创建窗口、渲染器和纹理，为显示做准备。
 * 并设置窗口标题为 "riscv32-NEMU" 等。
 ***************************************************************************************/
static void init_screen() {
  SDL_Window *window = NULL;
  char title[128];
  sprintf(title, "%s-NEMU", str(__GUEST_ISA__));
  SDL_Init(SDL_INIT_VIDEO);
  SDL_CreateWindowAndRenderer(
      SCREEN_W * (MUXDEF(CONFIG_VGA_SIZE_400x300, 2, 1)),
      SCREEN_H * (MUXDEF(CONFIG_VGA_SIZE_400x300, 2, 1)),
      0, &window, &renderer);
  SDL_SetWindowTitle(window, title);
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
      SDL_TEXTUREACCESS_STATIC, SCREEN_W, SCREEN_H);
  SDL_RenderPresent(renderer);
}

/***************************************************************************************
 * update_screen - 刷新 SDL 屏幕内容.(将 vmem 中的显存数据纹理化，并复制到 SDL 渲染器，最终呈现到窗口。) 调库, 不看了. 
*  
* 调用层级: device_update()调用vga_update_screen(), 后者在检查到sync寄存器非0时调用update_screen().
 ***************************************************************************************/
static inline void update_screen() {
  SDL_UpdateTexture(texture, NULL, vmem, SCREEN_W * sizeof(uint32_t));
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  SDL_RenderPresent(renderer);
}
#else
static void init_screen() {}

/***************************************************************************************
 * update_screen - 刷新屏幕 (AM-Native 模式)
 * 
 * 【功能】
 * 当 NEMU 编译为 AM 的 native 程序时使用。
 * 直接调用 AM IOE 接口将 vmem 内容绘制出去。
 ***************************************************************************************/
static inline void update_screen() {
  io_write(AM_GPU_FBDRAW, 0, 0, vmem, screen_width(), screen_height(), true);
}
#endif
#endif


//
/***************************************************************************************
 * vga_update_screen - 检查并处理屏幕刷新同步信号. 在检查到sync寄存器非0时调用update_screen().
 * 
 * 调用层级: device_update()调用vga_update_screen().
 ***************************************************************************************/
void vga_update_screen() {
  // 当 sync 寄存器非零时调用 update_screen()，然后将 sync 寄存器清零
  if (vgactl_port_base[1]) {  // vgactl_port_base[1] 是 sync 寄存器
    IFDEF(CONFIG_VGA_SHOW_SCREEN, update_screen());
    vgactl_port_base[1] = 0;
  }
}


/***************************************************************************************
 * init_vga - 初始化 VGA 设备
 * 
 * 【功能】
 * 1. 分配 VGA 控制寄存器空间 (vgactl)。
 * 2. 根据 CONFIG_VGA_SIZE宏 初始化屏幕宽高信息寄存器。
 * 3. 映射 VGA 控制寄存器到 MMIO 或 IO 空间 (vgactl, 默认 0xa0000100)。
 * 4. 分配帧缓冲显存空间 (vmem)。
 * 5. 映射帧缓冲到 MMIO 空间 (vmem, 默认 0xa1000000)。
 * 6. 初始化 SDL 图形库 (如果启用显示)。
 ***************************************************************************************/
void init_vga() {
  vgactl_port_base = (uint32_t *)new_space(8);
  vgactl_port_base[0] = (screen_width() << 16) | screen_height();
#ifdef CONFIG_HAS_PORT_IO
  add_pio_map ("vgactl", CONFIG_VGA_CTL_PORT, vgactl_port_base, 8, NULL);
#else
  add_mmio_map("vgactl", CONFIG_VGA_CTL_MMIO, vgactl_port_base, 8, NULL);
#endif

  vmem = new_space(screen_size());
  add_mmio_map("vmem", CONFIG_FB_ADDR, vmem, screen_size(), NULL);
  IFDEF(CONFIG_VGA_SHOW_SCREEN, init_screen());
  IFDEF(CONFIG_VGA_SHOW_SCREEN, memset(vmem, 0, screen_size()));
}
