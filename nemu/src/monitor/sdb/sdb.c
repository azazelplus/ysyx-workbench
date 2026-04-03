
//实现简易调试器sdb.


#include <isa.h>
#include <cpu/cpu.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <memory/paddr.h> //为了在cmd_x中使用paddr_read()来读取物理内存.
#include "sdb.h"

static int is_batch_mode = false;

void init_regex();
void init_wp_pool();


/*******************************cmd_x Forward declarations********************************/
/* Forward declarations for all command handlers */
static int cmd_c(char *args);
static int cmd_q(char *args);
static int cmd_si(char *args);
static int cmd_help(char *args);
static int cmd_info(char *args);
static int cmd_x(char *args);
static int cmd_p(char *args);
#ifdef CONFIG_WATCHPOINT
static int cmd_w(char *args);
static int cmd_d(char *args);
#endif
/*******************************cmd_x Forward declarations********************************/




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



/**************************************命令表***************************************/
//sdb的命令表cmd_table. 它是static, 只在sdb.c可见.
//这是一个匿名结构体, 并且定义后马上创建了一个实例cmd_table [].
//cmd_table数组存的就是nemu的所有命令.
//第三个成员int (*handler) (char *)是一个返回类型为int, 参数为char*的函数指针.
static struct {
  const char *name;
  const char *description;
  int (*handler) (char *);
} cmd_table [] = {
  { "help", "Display information about all supported commands", cmd_help },
  { "c", "Continue the execution of the program", cmd_c },
  { "q", "Exit NEMU", cmd_q },
  { "si", "Step one instruction exactly. Usage: si [N]", cmd_si },
  { "info", "Print program status", cmd_info },
  { "x", "Examine memory. Usage: x N EXPR", cmd_x },
  { "p", "Print the value of an expression EXPR. Usage: p EXPR", cmd_p },
#ifdef CONFIG_WATCHPOINT
  { "w", "Set a watchpoint for an expression EXPR. Usage: w EXPR", cmd_w },
  { "d", "Delete a watchpoint with given NO. Usage: d NO", cmd_d },
#endif

  /* TODO: Add more commands */

};

//NR_CMD即number of commands. 计算命令表长度. ARRLEN宏就是计算数组长度. cmd_table是命令表.
#define NR_CMD ARRLEN(cmd_table)
/******************************************************************************/





/**************************************cmd_xxx*******************************************/
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

//single step cmd. 单步执行N条指令. N默认为1.
static int cmd_si(char *args) {
  int n = 1;  // 默认执行1条指令
  
  // 如果有参数, 解析参数
  if (args != NULL) {
    n = atoi(args);  // 将字符串转为整数
    if (n <= 0) {
      printf("Invalid argument. N must be a positive integer.\n");
      return 0;
    }
  }
  
  cpu_exec(n);  // 执行n条指令
  return 0;
}

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

// info cmd. info r/w: info r打印寄存器状态; info w打印监视点状态.
static int cmd_info(char *args){
  if (args == NULL) 
  {
    printf("Usage: info r/w\n");
    return 0;
  }

  if(strcmp(args, "r")==0){
    //打印寄存器状态
    isa_reg_display();  //调用ISA层函数. 
  } 
#ifdef CONFIG_WATCHPOINT
  else if (strcmp(args, "w")==0){
    //打印监视点状态
    display_wp();
  }
#endif
  return 0;
}

//扫描内存:x N EXPR 求出表达式EXPR的值, 将结果作为起始内存地址, 以十六进制形式输出连续的N个4字节(N个32bit)
//例子: x 10 0x80000000, x 4 $pc
static int cmd_x(char *args){
  if (args == NULL) {
    printf("Usage: x N EXPR\n");
    printf("Example: x 10 0x80000000, x 4 $pc\n");
    return 0;
  }
  
  // 提取第一个参数 N (要输出的字节数)
  //strtok: 这是一个状态机迭代器函数. 它改写传入的字符串. 
  // 第一次调用: 传入字符串s和分隔符(字符串, 但是会被看作一个一个字符)delim.  它将 s 中 第一个遇到的 delim(此处为" ")(如果delim传入的是字符串, 则其中每个单字符都参与匹配, 仍然只匹配成功一次)替换为'\0', 并返回 第一个token(字符串)的指针. 
  // 下一次调用 n_str 时, 传入NULL, 它会继续从上次停止的位置继续查找下一个token. 但是被读的字符串args被改写了, arg还是指向这串包含'\0'的字符串.
  char *n_str = strtok(args, " ");
  if (n_str == NULL) {
    printf("Error: missing argument N\n");
    return 0;
  }
  
  // 写入N
  int n = atoi(n_str);
  if (n <= 0) {
    printf("Error: N must be a positive integer\n");
    return 0;
  }
  
  // 提取第二个参数作为表达式 EXPR
  // strtok(NULL, " ") 会继续从上次位置解析下一个 token
  char *expr_str = strtok(NULL, " ");
  if (expr_str == NULL) {
    printf("Error: missing expression EXPR\n");
    return 0;
  }
  
  // 对表达式 expr_str 求值，得到起始地址
  bool success = false;
  word_t addr = expr(expr_str, &success);
  
  if (!success) {
    printf("Error: invalid expression '%s'\n", expr_str);
    return 0;
  }
  
  // 输出内存内容：每行显示 4 个字节（一个 word）
  printf("Memory at 0x%08x:\n", addr);
  for (int i = 0; i < n; i++) {
    // 每 4 个字节打印一行
    if (i % 4 == 0) {
      printf("0x%08x: ", addr + i * 4);
    }
    
    // 读取 4 字节并打印
    word_t data = paddr_read(addr + i * 4, 4);
    printf("0x%08x ", data);
    
    // 每 4 个 word 换行
    if ((i + 1) % 4 == 0 || i == n - 1) {
      printf("\n");
    }
  }
  return 0;
}


// p EXPR: 表达式求值命令
static int cmd_p(char *args) {
  if (args == NULL) {
    printf("Usage: p EXPR\n");
    printf("Example: p 1+2, p 0x1000-10, p $pc\n");
    return 0;
  }
  
  //
  bool success = false;
  word_t result = expr(args, &success);
  
  if (success) {
    printf("Result: %u (0x%x)\n", result, result);
  } else {
    printf("Invalid expression: %s\n", args);
  }
  
  return 0;
}


#ifdef CONFIG_WATCHPOINT
// watchpoint: w EXPR 设置监视点. 当表达式 EXPR 的值发生变化时, 暂停程序执行
// example: w *0x80002000	
static int cmd_w(char *args){
  if (args == NULL) {
    printf("Usage: w EXPR\n");
    printf("Example: w $a0, w *0x80000000\n");
    return 0;
  }
  
  // 先对表达式求值，检查表达式是否有效
  bool success = false;
  word_t value = expr(args, &success);
  
  if (!success) {
    printf("Error: invalid expression '%s'\n", args);
    return 0;
  }
  
  // 创建新的监视点
  int no = create_wp(args, value);
  if (no < 0) {
    printf("Error: failed to create watchpoint\n");
    return 0;
  }
  
  printf("Watchpoint %d: %s = 0x%08x\n", no, args, value);
  
  return 0;
}


// delete watchpoint: d NO 删除监视点
static int cmd_d(char *args){
  if (args == NULL) {
    printf("Usage: d NO\n");
    printf("Example: d 1, d 3\n");
    return 0;
  }
  
  // 解析监视点编号
  int no = atoi(args);
  
  // 删除监视点
  if (delete_wp(no) < 0) {
    printf("Error: watchpoint %d not found\n", no);
    return 0;
  }
  
  printf("Delete watchpoint %d\n", no);
  
  return 0;
}
#endif




/***********************************cmd_xxx  code end*************************************/








//设置为批处理模式.
void sdb_set_batch_mode() {
  is_batch_mode = true;
}


/***********************************nemu的命令行交互主循环*************************************/
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
      if (strcmp(cmd, cmd_table[i].name) == 0) //匹配cmd_table[i].name
      {
        if (cmd_table[i].handler(args) < 0) { return; } //执行对应命令. 将参数args传入.
        break;
      }
    }
    //如果命令不存在, 则打印错误信息.
    if (i == NR_CMD) { printf("Unknown command '%s'\n", cmd); }
/***********************************查找和执行命令****************************************/ 
  }
}
/********************************nemu的命令行交互主循环结束*********************************/



//sdb初始化. sdb是简化版gdb.
void init_sdb() {
  /* Compile the regular expressions. */
  init_regex();

  /* Initialize the watchpoint pool. */
  init_wp_pool();
}
