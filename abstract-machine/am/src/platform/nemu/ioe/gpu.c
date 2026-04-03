/**
 * IOE=input/output enterface, 输入输出界面层
 * IOE 显示设备（GPU）的am侧驱动. 它面向用户程序, 提供AM_GPU_CONFIG(AM_GPU_CONFIG_T *cfg)和AM_GPU_FBDRAW(AM_GPU_FBDRAW_T *ctl)接口. 
 * 如果对应nemu, 它的硬件实现是vga.c
 * 
 * 【设备功能】
 * - 配置：查询显示器分辨率和帧缓冲大小
 * - 绘图：向帧缓冲（VMEM）写入像素数据，通过 sync 操作显示到屏幕
 * - 状态：查询 GPU 是否就绪
 * 
 * 【AM API】. 客户程序调用它们即可. 它们是宏, 定义在 klib-macros.h 中
 * - io_read(AM_GPU_CONFIG, &cfg)   查询屏幕配置（分辨率等）
 * - io_write(AM_GPU_FBDRAW, &draw)  执行绘图操作
 * - io_read(AM_GPU_STATUS, &status)  查询 GPU 状态
 * 
 * 【NEMU 设备地址】
 * - VGACTL_ADDR = 0xa0000100  GPU 控制寄存器基址
 * - SYNC_ADDR = VGACTL_ADDR + 4  同步寄存器（触发屏幕更新）
 * 
 * 【当前实现】
 * - 简化实现：不模拟实际的帧缓冲操作
 * - 始终报告屏幕宽高为 0（表示虚拟设备）
 * - GPU 始终处于就绪状态（不需要等待）
 **/

#include <am.h>
#include <nemu.h>

#define SYNC_ADDR (VGACTL_ADDR + 4)

void __am_gpu_init() {
  //什麽也不做...
}



/**
 * __am_gpu_config - 读取 GPU 配置（屏幕分辨率）
 * 
 * 【参数说明】
 * @param cfg: 接收配置信息的结构体指针
 *             - width:  屏幕宽度 (像素)
 *             - height: 屏幕高度 (像素)
 *             - vmemsz: 显存大小 (字节)
 *             - present: 是否存在 GPU
 *             - has_accel: 是否支持硬件加速
 * 
 * 从 NEMU 的 VGACTL_ADDR 寄存器读取屏幕宽高等信息。
 **/
void __am_gpu_config(AM_GPU_CONFIG_T *cfg) {
  // 从 NEMU 的 vgactl 寄存器读取屏幕尺寸
  // vgactl_port_base[0] = (width << 16) | height
  uint32_t vgactl = inl(VGACTL_ADDR);
  uint32_t width  = vgactl >> 16;        // 高 16 位是宽度
  uint32_t height = vgactl & 0xFFFF;     // 低 16 位是高度
  *cfg = (AM_GPU_CONFIG_T) {
    .present = true, .has_accel = false,
    .width = width, .height = height,
    .vmemsz = width * height * sizeof(uint32_t)
  };
}


/**
 * __am_gpu_fbdraw - 绘制像素到帧缓冲 (Framebuffer draw)
 * 
 * 【参数说明】
 * @param ctl: 绘图控制信息结构体指针
 *             - x, y:   绘制区域左上角坐标
 *             - w, h:   绘制区域宽和高
 *             - pixels: 像素数据数组（行优先存储）
 *             - sync:   是否在绘制后立即刷新屏幕
 * 
 * 【说明】
 * 将 pixels 中的像素颜色数据复制到显存 (VMEM) 的对应位置。
 * 若 sync 为 true，则写入 SYNC_ADDR 触发硬件刷新屏幕。
 **/
void __am_gpu_fbdraw(AM_GPU_FBDRAW_T *ctl) {
  // 将像素数据写入 VMEM
  // ctl 包含: x, y (起始坐标), pixels (像素数据指针), w, h (绘制区域大小), sync (是否同步)
  uint32_t *pixels = ctl->pixels;
  int x = ctl->x, y = ctl->y, w = ctl->w, h = ctl->h;
  
  // 获取屏幕宽度用于计算偏移
  uint32_t vgactl = inl(VGACTL_ADDR);
  int screen_w = vgactl >> 16;
  
  // 将像素数据逐行写入 VMEM
  uint32_t *fb = (uint32_t *)(uintptr_t)FB_ADDR;
  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      fb[(y + j) * screen_w + (x + i)] = pixels[j * w + i];
    }
  }
  
  // 如果需要同步，设置 sync 寄存器触发屏幕刷新
  if (ctl->sync) {
    outl(SYNC_ADDR, 1);
  }
}

void __am_gpu_status(AM_GPU_STATUS_T *status) {
  status->ready = true;
}
