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
//表达式求值的实现.
***************************************************************************************/

#include <isa.h>

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 但是我想用ERE...TT
 */
#include <regex.h>


//枚举所有的token类型. 
enum {
  TK_NOTYPE = 256, 
  TK_EQ,       // 等于 ==
  TK_NEQ,      // 不等于 !=
  TK_AND,      // 逻辑与 &&
  TK_OR,       // 逻辑或 ||
  TK_NUM,      // 十进制数
  TK_HEX,      // 十六进制数
  TK_REG,      // 寄存器
  TK_DEREF,    // 指针解引用 (*)

  //单字符操作符 的 token_type 直接用 ASCII表示.
  /* TODO: Add more token types */

};


//定义正则表达式规则数组.
//注意涉及两个转义引擎: C编译器字符串转义引擎 和 正则表达式引擎.
/*
" +"---C编译器--->" +"---正则引擎--->匹配一个或多个空格字符
"\\+"---C编译器--->"\+"---正则引擎--->匹配字面的加号 '+'
"=="---C编译器--->"=="---正则引擎--->匹配字面的等于号 '=='



*/
static struct rule {
  const char *regex;
  int token_type;
} rules[] = {

  /* TODO: Add more rules.
   * Pay attention to the precedence level of different rules.
   */

  {" +", TK_NOTYPE},    // ` ` 优先级最高, 先匹配空格.
  {"\\+", '+'},         // `\+`
  {"==", TK_EQ},        // `==`
  // 多字符操作符要在单字符之前
  {"0[xX][0-9a-fA-F]+", TK_HEX},       // 十六进制(必须在 [0-9]+ 之前!)
  {"==", TK_EQ},                       // 双字符:必须在 '=' 之前
  {"!=", TK_NEQ},                      // 双字符:必须在 '!' 之前  
  {"&&", TK_AND},                      // 双字符:必须在 '&' 之前
  {"||", TK_OR},                       // 双字符:必须在 '|' 之前
  // 然后是单字符和多字符数字
  {"[0-9]+", TK_NUM},                  // 十进制数
  {"\\$[a-zA-Z_][a-zA-Z0-9_]*", TK_REG}, // 寄存器
  
  // 最后是单字符操作符. 单字符操作符 的 token_type 直接用 ASCII表示.
  {"\\+", '+'},
  {"\\-", '-'},
  {"\\*", '*'},
  {"/", '/'},
  {"\\(", '('},
  {"\\)", ')'},
};

#define NR_REGEX ARRLEN(rules)

// 编译好的正则表达式数组.
static regex_t re[NR_REGEX] = {};

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
// 初始化正则表达式. 编译所有的正则表达式规则.
void init_regex() {
  int i;
  char error_msg[128];
  int ret;

  for (i = 0; i < NR_REGEX; i ++) {
    ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    if (ret != 0) {
      regerror(ret, &re[i], error_msg, 128);
      panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
    }
  }
}

// 描述一个token的结构体. 分别描述token_type和token字符串.
typedef struct token {
  int type;
  char str[32];
} Token;


//数组tokens用来存放词法分析得到的token序列. nr_token记录token数量.
static Token tokens[32] __attribute__((used)) = {};
static int nr_token __attribute__((used))  = 0;


// make_token: 词法分析函数. 把字符串e分解成一个个token, 存到tokens数组中.
static bool make_token(char *e) {
  int position = 0;
  int i;
  regmatch_t pmatch;

  nr_token = 0;

  while (e[position] != '\0') {
    /* Try all rules one by one. */
    for (i = 0; i < NR_REGEX; i ++) {
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
        char *substr_start = e + position;
        int substr_len = pmatch.rm_eo;

        Log("match rules[%d] = \"%s\" at position %d with len %d: %.*s",
            i, rules[i].regex, position, substr_len, substr_len, substr_start);

        position += substr_len;

        /* TODO: Now a new token is recognized with rules[i]. Add codes
         * to record the token in the array `tokens'. For certain types
         * of tokens, some extra actions should be performed.
         */

        switch (rules[i].token_type) {
          default: TODO();
        }

        break;
      }
    }

    if (i == NR_REGEX) {
      printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
      return false;
    }
  }

  return true;
}


word_t expr(char *e, bool *success) {
  if (!make_token(e)) {
    *success = false;
    return 0;
  }

  /* TODO: Insert codes to evaluate the expression. */
  TODO();

  return 0;
}
