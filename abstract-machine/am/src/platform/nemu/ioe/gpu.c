/***************************************************************************************
 * 显示设备（GPU）IOE 实现
 * 
 * 【设备功能】
 * - 配置：查询显示器分辨率和帧缓冲大小
 * - 绘图：向帧缓冲（VMEM）写入像素数据，通过 sync 操作显示到屏幕
 * - 状态：查询 GPU 是否就绪
 * 
 * 【AM API】
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
 ***************************************************************************************/

#include <am.h>
#include <nemu.h>

#define SYNC_ADDR (VGACTL_ADDR + 4)

void __am_gpu_init() {
}

void __am_gpu_config(AM_GPU_CONFIG_T *cfg) {
  *cfg = (AM_GPU_CONFIG_T) {
    .present = true, .has_accel = false,
    .width = 0, .height = 0,
    .vmemsz = 0
  };
}

void __am_gpu_fbdraw(AM_GPU_FBDRAW_T *ctl) {
  if (ctl->sync) {
    outl(SYNC_ADDR, 1);
  }
}

void __am_gpu_status(AM_GPU_STATUS_T *status) {
  status->ready = true;
}
