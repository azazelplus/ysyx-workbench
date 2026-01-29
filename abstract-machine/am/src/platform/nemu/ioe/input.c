/***************************************************************************************
 * 键盘输入设备 IOE 实现
 * 
 * 【设备功能】
 * 通过 AM 库读取键盘输入。当用户按下或释放键盘按键时，
 * NEMU 会更新键盘设备的内部寄存器。
 * 
 * 【AM API】
 * io_read(AM_INPUT_KEYBRD, &kbd)  读取键盘事件
 * 
 * 【输出数据结构】 AM_INPUT_KEYBRD_T
 * - keydown: 1 表示按键按下，0 表示释放
 * - keycode: 按下/释放的键对应的键码值
 * 
 * 【当前实现】
 * 简化实现，总是返回无键盘事件（keycode=AM_KEY_NONE）。
 * 完整实现需要访问 NEMU 的键盘输入设备寄存器。
 ***************************************************************************************/

#include <am.h>
#include <nemu.h>

#define KEYDOWN_MASK 0x8000

void __am_input_keybrd(AM_INPUT_KEYBRD_T *kbd) {
  kbd->keydown = 0;
  kbd->keycode = AM_KEY_NONE;
}
