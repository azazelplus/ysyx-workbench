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
//实现简易调试器.
***************************************************************************************/

#include <isa.h>
#include <cpu/cpu.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "sdb.h"

static int is_batch_mode = false;

void init_regex();
void init_wp_pool();

/* We use the `readline' library to provide more flexibility to read from stdin. */
static char* rl_gets() {
  static char *line_read = NULL;

  if (line_read) {
    free(line_read);
    line_read = NULL;
  }

  line_read = readline("(nemu) ");

  if (line_read && *line_read) {
    add_history(line_read);
  }

  return line_read;
}


//continue cmd. 该函数让cpu从当前暂停位置继续一直执行下去. -1即最大整数, 希望执行无限多条指令，直到程序结束或被中断.
static int cmd_c(char *args) {
  cpu_exec(-1);
  return 0;
}

//quit cmd. 该函数让cpu退出模拟器.
static int cmd_q(char *args) {
  nemu_state.state = NEMU_QUIT; // 把nemu状态设置为NEMU_QUIT. 加上这一行可以解决不报错的问题.
  return -1;
}

static int cmd_help(char *args);


//sdb的命令表cmd_table. 它是static, 只在sdb.c可见.
//这是一个匿名结构体, 并且定义后马上创建了一个实例cmd_table [].
//cmd_table数组存的就是nemu的所有命令.
static struct {
  const char *name;
  const char *description;
  int (*handler) (char *);
} cmd_table [] = {
  { "help", "Display information about all supported commands", cmd_help },
  { "c", "Continue the execution of the program", cmd_c },
  { "q", "Exit NEMU", cmd_q },

  /* TODO: Add more commands */

};

//NR_CMD即number of commands. 计算命令表长度. ARRLEN宏就是计算数组长度. cmd_table是命令表.
#define NR_CMD ARRLEN(cmd_table)

static int cmd_help(char *args) {
  /* extract the first argument */
  char *arg = strtok(NULL, " ");
  int i;

  if (arg == NULL) {
    /* no argument given */
    for (i = 0; i < NR_CMD; i ++) {
      printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
    }
  }
  else {
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(arg, cmd_table[i].name) == 0) {
        printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
        return 0;
      }
    }
    printf("Unknown command '%s'\n", arg);
  }
  return 0;
}

//设置为批处理模式.
void sdb_set_batch_mode() {
  is_batch_mode = true;
}


// nemu的命令行交互主循环. 整个是一个字符串处理.
void sdb_mainloop() {
  //如果是批处理模式, 执行cmd_c(继续无限(-1)次执行程序). 不会进入交互命令行.
  if (is_batch_mode) {
    cmd_c(NULL);
    return;
  }

  //如果不是批处理模式, 则进入命令行交互循环.
  //rl_gets即read line. 该函数返回用户输入的一整行字符串.
  //主循环结构: 每次循环用rl_gets获取一行用户输入str, 直到rl_gets()返回NULL, 意味着用户按ctrl或EOF.
  for (char *str; (str = rl_gets()) != NULL; ) {
    // str_end是字符串str结尾指针.
    char *str_end = str + strlen(str);

    /* extract the first token as the command */
    // 提取命令. cmd是str的第一个单词(strtok()读str直到遇到第一个空格, 拿取第一个单词). 所以cmd就是用户输入的该行指令名.
    char *cmd = strtok(str, " ");
    // 如果cmd啥也没取到, 说明用户输入的是空行, 则继续下一次循环.
    if (cmd == NULL) { continue; }


    /* treat the remaining string as the arguments,
     * which may need further parsing
     */
    //提取命令后面的参数, 存到args.
    char *args = cmd + strlen(cmd) + 1;
    if (args >= str_end) {
      args = NULL;
    }

// 如果有配置设备模型, 则清空SDL事件队列.
#ifdef CONFIG_DEVICE
    extern void sdl_clear_event_queue();
    sdl_clear_event_queue();
#endif

//下面是一个命令行解释器REPL.
/***********************************查找和执行命令****************************************/ 
    //查找命令表, 执行对应的命令处理函数. NR_CMD=3, 是命令表长度.
    int i;
    //遍历整个命令表.
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(cmd, cmd_table[i].name) == 0) {
        if (cmd_table[i].handler(args) < 0) { return; }
        break;
      }
    }
    //如果命令不存在, 则打印错误信息.
    if (i == NR_CMD) { printf("Unknown command '%s'\n", cmd); }
/***********************************查找和执行命令****************************************/ 
  }
}

//sdb初始化. sdb是简化版gdb.
void init_sdb() {
  /* Compile the regular expressions. */
  init_regex();

  /* Initialize the watchpoint pool. */
  init_wp_pool();
}
