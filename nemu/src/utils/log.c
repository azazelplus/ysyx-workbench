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

#include <common.h>

extern uint64_t g_nr_guest_inst;

//如果在AM目标下, 不进行日志初始化.
#ifndef CONFIG_TARGET_AM
// 写日志指针. 指针都会写到这里去.
FILE *log_fp = NULL;

// 初始化日志系统. 在monitor.c中的init_monitor()调用. 最终由nemu-main()调用init_monitor()实现初始化
void init_log(const char *log_file) {
  log_fp = stdout;
  if (log_file != NULL) {
    FILE *fp = fopen(log_file, "w");
    Assert(fp, "Can not open '%s'", log_file);
    log_fp = fp;  //fopen 成功，就把全局 log_fp 指向新打开的文件流. 从此后日志会写入该文件而不是 stdout.
  }
  Log("Log is written to %s", log_file ? log_file : "stdout");
}

// 检查当前是否启动日志. 需要满足两个条件: 
// 1.Kconfig开启了TRACE, 从而CONFIG_TRACE=1
// 2.当前指令
bool log_enable() {
  return MUXDEF(CONFIG_TRACE, (g_nr_guest_inst >= CONFIG_TRACE_START) &&
         (g_nr_guest_inst <= CONFIG_TRACE_END), false);
}
#endif
