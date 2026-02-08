/***************************************************************************************
* Copyright (c) 2014-2022 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <isa.h>
#include <memory/paddr.h>

// this is not consistent with uint8_t
// but it is ok since we do not access the array directly

//img就是内置程序. static const意味着不导出符号, 只能由本.c文件内的init_isa()函数调用. 内容不变, 存放在只读存储区.
static const uint32_t img [] = {
  0x00000297,  // auipc t0,0        把当前pc+0(还是pc), 放入寄存器t0
  0x00028823,  // sb  zero,16(t0)   把zero(=0x0)存入内存地址[t0+16]. 即在此写一byte的0
  0x0102c503,  // lbu a0,16(t0)     从内存地址[t0+16]读一byte到寄存器a0中. 之前存入的是0, 所以a0=0
  0x00100073,  // ebreak (used as nemu_trap)  触发一个ebreak异常, 进入nemu_trap处理函数
  0xdeadbeef,  // some data
};

//初始化寄存器. 
static void restart() {
  /* Set the initial program counter. */
  cpu.pc = RESET_VECTOR;

  /* The zero register is always 0. */
  cpu.gpr[0] = 0;

  /* 初始化mstatus for difftest (MPP=11, FS=00) */
  cpu.mstatus = 0x1800;
}

//进行一些ISA相关的初始化工作.
void init_isa() {
  /* Load built-in image. 将一个内置的客户程序读入到内存中. */

  //内置客户程序放在nemu/src/isa/$ISA/init.c中. 内置客户程序的行为非常简单, 它只包含少数几条指令, 甚至算不上在做一些有意义的事情.
  //把客户程序读入到一个固定的内存位置RESET_VECTOR. RESET_VECTOR的值在nemu/include/memory/paddr.h中定义.
  memcpy(guest_to_host(RESET_VECTOR), img, sizeof(img));

  /* Initialize this virtual computer system. 初始化寄存器.*/
  restart();
}
