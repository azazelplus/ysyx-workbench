#include "config.h"
#if ENABLE_SDB

#include "sdb.h"
#include "cpu.h"
#include "dpic.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <readline/readline.h>
#include <readline/history.h>

//continue
static int cmd_c(char *args) {
    cpu_exec(-1);
    return 0;
}

//quit
static int cmd_q(char *args) {
    return -1;
}

//single step
static int cmd_si(char *args) {
    uint64_t n = 1;
    if (args) {
        sscanf(args, "%lu", &n);
    }
    cpu_exec(n);
    return 0;
}

static int cmd_info(char *args) {
    if (args == NULL) {
        printf("Usage: info [r]\n");
        return 0;
    }
    if (strcmp(args, "r") == 0) {
        uint32_t *regs = get_cpu_regs();
        if (!regs) {
            printf("Error: CPU registers not available (DUT not set?)\n");
            return 0;
        }
        const char *reg_names[] = {
            "$0", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
            "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
            "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
            "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
        };
        for (int i = 0; i < 32; i++) {
            printf("%-3s = 0x%08x  ", reg_names[i], regs[i]);
            if ((i + 1) % 4 == 0) printf("\n");
        }
    }
    return 0;
}

//x N ADDR, 从地址 ADDR 开始打印 N 个 4 byte数据
static int cmd_x(char *args) {
    if (args == NULL) {
        printf("Usage: x N ADDR\n");
        return 0;
    }
    int n;
    unsigned int addr;
    if (sscanf(args, "%d %x", &n, &addr) != 2) {
        printf("Invalid usage. x N ADDR (hex)\n");
        return 0;
    }
    for (int i = 0; i < n; i++) {
        uint32_t data = pmem_read(addr + i * 4);
        printf("0x%08x: 0x%08x\n", addr + i * 4, data);
    }
    return 0;
}

static int cmd_help(char *args);

static struct {
    const char *name;
    const char *description;
    int (*handler) (char *);
} cmd_table [] = {
    { "help", "Display informations about all supported commands", cmd_help },
    { "c", "Continue the execution of the program", cmd_c },
    { "q", "Exit", cmd_q },
    { "si", "Step one instruction exactly (si [N])", cmd_si },
    { "info", "Generic info (info r: registers)", cmd_info },
    { "x", "Scan memory (x N ADDR)", cmd_x },
};

#define NR_CMD (sizeof(cmd_table) / sizeof(cmd_table[0]))

//help
static int cmd_help(char *args) {
    for (int i = 0; i < NR_CMD; i ++) {
        printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
    }
    return 0;
}

//sdb主循环
void sdb_mainloop() {
    printf("Welcome to NPC Simple Debugger. Type 'help' for available commands.\n");
    //主循环: 监听输入命令
    for (char *str; (str = readline("(npc) ")) != NULL; )//readline("tip")打印出tip并等待接收一个字符串然后返回.
    {
        char *str_end = str + strlen(str);  //str_end指向当前命令字符串str的结尾
        char *cmd = strtok(str, " ");   //strtok用空格分割字符串, 返回第一个子串指针作为命令字符串cmd.
        //输入为空: 继续.
        if (cmd == NULL) { 
            free(str);
            continue; 
        }

        char *args = cmd + strlen(cmd) + 1; //提取参数部分,
        if (args >= str_end) { args = NULL;}    //如果没有参数, 则置空.

        // 查找sdb命令表并执行.
        size_t i;
        for (i = 0; i < NR_CMD; i ++) {
            if (strcmp(cmd, cmd_table[i].name) == 0) {
                if (cmd_table[i].handler(args) < 0) { 
                    free(str);
                    return; 
                }
                break;
            }
        }

        //没查到: 报错.
        if (i == NR_CMD) { printf("Unknown command '%s'\n", cmd); }
        add_history(str);   //记录指令. 这是clib库函数, 这样你就可以用↑↓箭浏览指令了...
        free(str);
    }
}

#endif // ENABLE_SDB
