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
//定义全局变量nemu_state来保存模拟器的当前运行状态.
//提供状态检查函数is_exit_status_bad() —— 判断程序是否“异常退出”.
***************************************************************************************/

#include <utils.h>

//声明全局变量nemu_state. 初始值
NEMUState nemu_state = { .state = NEMU_STOP };

//根据变量nemu_state.state的值给出bool判断结果.
int is_exit_status_bad() {
  int good = (nemu_state.state == NEMU_END && nemu_state.halt_ret == 0) ||
             (nemu_state.state == NEMU_QUIT); //是否: 正常结束(状态码为NEMU_END 且 上一个程序退出码=0) 或 用户请求退出(state == NEMU_QUIT)
  return !good;
}
