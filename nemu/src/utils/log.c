/***************************************************************************************
日志宏. 挂载点:
* 
* 日志生成地址见nemu.mk的NEMUFLAGS的-l选项.
***************************************************************************************/

#include <common.h>

extern uint64_t g_nr_guest_inst;

//如果在AM目标下, 不进行日志初始化.
#ifndef CONFIG_TARGET_AM
// 写日志指针. 指针都会写到这里去.
FILE *log_fp = NULL;


/** init_log - 初始化日志系统. 在monitor.c中的init_monitor()调用. 最终由nemu-main()调用init_monitor()实现初始化.
* 在init_monitor()中被调用.
* @param log_file: 日志文件路径. 如果为NULL, 则日志输出到stdout.
* @return: null
*/
void init_log(const char *log_file) {
  log_fp = stdout;  //默认日志输出到标准输出. 
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
