/***************************************************************************************
* main函数.
***************************************************************************************/

#include <common.h>

void init_monitor(int, char *[]);
void am_init_monitor();
void engine_start();
int is_exit_status_bad();

int main(int argc, char *argv[]) {
  /* Initialize the monitor. */
  // AM refers to: Abstract Machine. 当NEMU作为模拟机后端的时候使用.
#ifdef CONFIG_TARGET_AM
  am_init_monitor();
#else
  init_monitor(argc, argv);
#endif

  /* Start engine. */
  //nemu运行层级:  main() -> engine_start() -> sdb_mainloop()主循环 -> 接收用户命令, 交给cmd_[命令]()函数 -> (对于cmd_q()这里就是结束了)对cmd_c() -> cpu_exec() -> cpu_exec_once()单步执行下指令
  engine_start();

  return is_exit_status_bad();
}
