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
#include <memory/paddr.h>

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 但是我想用ERE...TT
 */
#include <regex.h>




//枚举所有的token.type. 
typedef enum {
  TK_NOTYPE = 256, 
  TK_EQ = 257,       // 等于 ==
  TK_NEQ = 258,      // 不等于 !=
  TK_AND = 259,      // 逻辑与 &&
  TK_OR = 260,       // 逻辑或 ||
  TK_NUM = 261,      // 十进制数
  TK_HEX = 262,      // 十六进制数
  TK_REG = 263,      // 寄存器
  TK_DEREF = 264,    // 指针解引用 (*)
  TK_NEG = 265,      // 一元负号 (-)

  /*
  单字符操作符 的 token_type 直接用 ASCII表示:
  '+' = 43
  '-' = 45
  '*' = 42
  '/' = 47
  '(' = 40
  ')' = 41

  */

  /* TODO: Add more token types */
} TokenType;


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

  // 优先级1: 空格 (最高优先级,总是先匹配)
  {" +", TK_NOTYPE},
  
  // 优先级2: 多字符操作符 (必须在单字符之前!)
  {"==", TK_EQ},                       // 等于
  {"!=", TK_NEQ},                      // 不等于
  {"&&", TK_AND},                      // 逻辑与
  {"\\|\\|", TK_OR},                   // 逻辑或 (需要转义两个 |)
  
  // 优先级3: 十六进制数 (必须在十进制之前!)
  {"0[xX][0-9a-fA-F]+", TK_HEX},
  
  // 优先级4: 十进制数
  {"[0-9]+", TK_NUM},
  
  // 优先级5: 寄存器
  {"\\$[a-zA-Z_][a-zA-Z0-9_]*", TK_REG},
  
  // 优先级6: 单字符操作符
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


//全局数组tokens用来存放make_token()遍历字符串 e 得到的token序列. nr_token是token数量. nr=number
static Token tokens[32] __attribute__((used)) = {};
static int nr_token __attribute__((used))  = 0;


// make_token: 词法分析函数. 把字符串e分解成一个个token, 存到tokens数组中.
static bool make_token(char *e) {
  int position = 0; // 当前解析到e的第position个字符.
  int i;
  regmatch_t pmatch;

  nr_token = 0;
  //对字符串e逐字符遍历
  while (e[position] != '\0') {

    /* 对rule中所有规则遍历. Try all rules one by one. */
    for (i = 0; i < NR_REGEX; i ++) {
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) 
      {
        //substr_start是匹配子串的起始地址. substr_len是匹配子串的长度.
        char *substr_start = e + position;
        int substr_len = pmatch.rm_eo;

        Log("match rules[%d] = \"%s\" at position %d with len %d: %.*s",
            i, rules[i].regex, position, substr_len, substr_len, substr_start);

        position += substr_len; //移动指针

        /* TODO: Now a new token is recognized with rules[i]. Add codes
         * to record the token in the array `tokens'. For certain types
         * of tokens, some extra actions should be performed.
         */
        /****************************根据 token 类型处理**************************** */
        switch (rules[i].token_type) {

          // 空格直接丢弃,不记录到 tokens 数组
          case TK_NOTYPE:            
            break;
          
          // 单字符操作符(除了-, *要特殊处理因为有一元和二元之分): 直接用 ASCII 码作为 type
          case '+':
          case '/':
          case '(':
          case ')':
            tokens[nr_token].type = rules[i].token_type;
            tokens[nr_token].str[0] = rules[i].token_type;  // 单字符
            tokens[nr_token].str[1] = '\0';                 // 结束符
            nr_token++;
            break;
          
          // 负号 '-': 需要判断是一元还是二元
          // -和* 的一二元判断都遵循简单逻辑: 如果前面一个token是运算符、左括号或者是第一个 token，则是一元运算符.
          case '-': {
            bool is_unary = false;
            // 如果是第一个 token，肯定是一元负号
            if (nr_token == 0) {
              is_unary = true;
            } else {
              // 检查前一个 token 的类型
              int prev_type = tokens[nr_token - 1].type;
              // 如果前面是运算符或左括号，则是一元负号
              if (prev_type == '+' || prev_type == '-' || prev_type == '*' || 
                  prev_type == '/' || prev_type == '(' ||
                  prev_type == TK_EQ || prev_type == TK_NEQ ||
                  prev_type == TK_AND || prev_type == TK_OR ||
                  prev_type == TK_DEREF || prev_type == TK_NEG) {
                is_unary = true;
              }
            }
            
            // 根据判断结果设置 token 类型
            tokens[nr_token].type = is_unary ? TK_NEG : '-';
            tokens[nr_token].str[0] = '-';
            tokens[nr_token].str[1] = '\0';
            nr_token++;
            break;
          }
          
          // 星号 '*': 需要判断是解引用还是乘法
          case '*': {
            bool is_deref = false;
            // 如果是第一个 token，肯定是解引用
            if (nr_token == 0) {
              is_deref = true;
            } else {
              // 检查前一个 token 的类型
              int prev_type = tokens[nr_token - 1].type;
              // 如果前面是运算符或左括号，则是解引用
              if (prev_type == '+' || prev_type == '-' || prev_type == '*' || 
                  prev_type == '/' || prev_type == '(' ||
                  prev_type == TK_EQ || prev_type == TK_NEQ ||
                  prev_type == TK_AND || prev_type == TK_OR ||
                  prev_type == TK_DEREF || prev_type == TK_NEG) {
                is_deref = true;
              }
            }
            
            // 根据判断结果设置 token 类型
            tokens[nr_token].type = is_deref ? TK_DEREF : '*';
            tokens[nr_token].str[0] = '*';
            tokens[nr_token].str[1] = '\0';
            nr_token++;
            break;
          }
          
          // 多字符 token: 需要从原字符串复制
          case TK_NUM:      // 十进制数
          case TK_HEX:      // 十六进制数
          case TK_REG:      // 寄存器
          case TK_EQ:       // ==
          case TK_NEQ:      // !=
          case TK_AND:      // &&
          case TK_OR:       // ||
            tokens[nr_token].type = rules[i].token_type;
            // 将匹配到的子串复制到 tokens[nr_token].str
            strncpy(tokens[nr_token].str, substr_start, substr_len);
            tokens[nr_token].str[substr_len] = '\0';  // 手动添加字符串结束符
            nr_token++;
            break;
          
          default: 
            printf("Unhandled token type: %d\n", rules[i].token_type);
            return false;
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



// 调试函数: 打印所有 token (用于测试词法分析)
static void print_tokens() {
  printf("Total tokens: %d\n", nr_token);
  for (int i = 0; i < nr_token; i++) {
    printf("Token[%d]: type=%d, str=\"%s\"\n", 
           i, tokens[i].type, tokens[i].str);
  }
}


// 对解析好的表达式求值.
// 优先级:
// 1. 逻辑或    ||         (最低优先级)
// 2. 逻辑与    &&
// 3. 相等性    ==  !=
// 4. 加减法    +   -
// 5. 乘除法    *   /      (最高优先级)
// 6. 一元      负号-  解引用*
// 7. 括号      ( )
// 8. 原子      数字  寄存器
/************************* 递归下降表达式求值 *************************/

// 前向声明
static word_t eval_expr(int p, int q, bool *success);


// check_parentheses(p,q) 检查tokens[p..q]是否被一对匹配的括号包围. 例子↓
// 2 + 1                 // false, 没有被括号包围
// "(2 - 1)"             // true
// "(4 + 3 * (2 - 1))"   // true
// "4 + 3 * (2 - 1)"     // false, the whole expression is not surrounded by a matched pair of parentheses.
// "(4 + 3)) * ((2 - 1)" // false, bad expression.
// "(4 + 3) * (2 - 1)"   // false, the leftmost '(' and the rightmost ')' are not matched
static bool check_parentheses(int p, int q) {

  //如果两端不都是括号包裹, 直接返回false.
  if (
    tokens[p].type != '(' 
    ||                            // C中||逻辑或, |按位或
    tokens[q].type != ')'
  ) 
  {
    return false;
  }
  
  int count = 0;
  for (int i = p; i <= q; i++) {
    if (tokens[i].type == '(') count++;
    else if (tokens[i].type == ')') count--;
    
    // 如果中间某处 count 变为 0,说明括号不是包围整个表达式.
    // 例如: (1+2)+(3+4) 在中间会变成0
    if (count == 0 && i < q) {
      return false;
    }
  }
  
  return count == 0;
}

// 获取运算符优先级 (数字越小优先级越低)
static int get_op_priority(int type) {
  switch (type) {
    case TK_OR:  return 1;   // 逻辑或|| 优先级最低 = 1
    case TK_AND: return 2;   // &&
    case TK_EQ:
    case TK_NEQ: return 3;   // == !=
    case '+':
    case '-':    return 4;   // + -
    case '*':
    case '/':    return 5;   // 乘除: 优先级最高 = 5
    default:     return 0;   // 不是运算符
  }
}


// (保证token[p]..tokenp[q]已经是完整括号包裹的前提下)  找到主运算符的位置.
//  筛选逻辑: 是运算符 && 不出现在一对括号中 && 优先级最低 && 最靠右的token
static int find_main_op(int p, int q) {
  int main_op = -1;           // 主运算符位置
  int min_priority = 999;     // 最小优先级
  int paren_level = 0;        // 括号层级
  
  //遍历 tokens[p..q]
  for (int i = p; i <= q; i++) {

/***************************筛选: 不出现在一对括号中******************************/
    //如果扫到`(`, 括号层级加一层. 遍历一开始token[p]==`(`, 一定会paren_level++
    if (tokens[i].type == '(') {
      paren_level++;
      continue;
    }
    //如果扫到`)`, 括号层级褪一层
    if (tokens[i].type == ')') {
      paren_level--;
      continue;
    }
    
    // paren_level > 0 说明有括号层级, 该token在括号里. 括号内的运算符不考虑
    if (paren_level > 0) continue;
/***************************筛选: 不出现在一对括号中******************************/


/***************************筛选: 优先级最低 + 最靠右******************************/
    int priority = get_op_priority(tokens[i].type);
    if (priority > 0 && priority <= min_priority) {
      // 优先级更低,或同优先级但更靠右(确保左结合)
      min_priority = priority;  //更新最小优先级
      main_op = i;            //更新主运算符位置
    }
/***************************筛选: 优先级最低 + 最靠右******************************/
  } 
  return main_op;
}


// 找到从位置 p 开始的最小操作数的结束位置
// 用于确定一元运算符的操作数范围
// 最小操作数可以是: 数字、寄存器、或者一对匹配的括号
static int find_unary_operand_end(int p, int q) {
  if (p > q) return -1;
  
  // 如果是数字、十六进制数或寄存器，操作数就是它本身
  if (tokens[p].type == TK_NUM || tokens[p].type == TK_HEX || tokens[p].type == TK_REG) {
    return p;
  }
  
  // 如果是左括号，找到匹配的右括号
  if (tokens[p].type == '(') {
    int paren_level = 0;
    for (int i = p; i <= q; i++) {
      if (tokens[i].type == '(') paren_level++;
      else if (tokens[i].type == ')') {
        paren_level--;
        if (paren_level == 0) return i;  // 找到匹配的右括号
      }
    }
    return -1;  // 括号不匹配
  }
  
  // 如果是另一个一元运算符，递归查找
  if (tokens[p].type == TK_NEG || tokens[p].type == TK_DEREF) {
    return find_unary_operand_end(p + 1, q);
  }
  
  return -1;  // 无效的操作数
}


// 表达式求值函数. 递归求值.
// 最终使用形式为 eval_expr(0, nr_token - 1, success);
// 这个递归函数 拿到参数 p,q(即一段tokens)后, 可以处理四件事:
// 1. 递归基: 如果只有单token, 直接返回值.
// 2. 如果表达式被()包围, 去掉括号.
// 3. 处理一元运算符.
// 4. 找主运算符, 递归求值左右子表达式.
// 理解递归的原则: 不要试图跟踪整个递归过程，而是相信递归函数能正确解决子问题(eval_expr(p,q)总是能黑箱式地给出最终结果, 我们只需要把当前问题调用它来解决.)，我们只需要关注当前层次的处理。
static word_t eval_expr(int p, int q, bool *success) {

  if (p > q) {
    printf("Error: bad expression (p > q)\n");
    *success = false;
    return 0;
  }
  

  // 1.递归基: 单个 token: 数字或寄存器.
  // 注意此处发生显式转换: unsigned int --> word_t. 在ubuntu系统, 是64bit->32bit截断. 
  // strtoul()可以处理64bit. 但是我们约定只用32bit. 第二个参数endptr为按指针传递的方式返回"第一个不能转换的字符位置", 不关心可以NULL.
  if (p == q) {
    // 如果是单个token, 就可以直接处理了.

    // 单个10/16进制数字
    if (tokens[p].type == TK_NUM) {
      return (word_t)strtoul(tokens[p].str, NULL, 10);
    } else if (tokens[p].type == TK_HEX) {
      return (word_t)strtoul(tokens[p].str, NULL, 16);
    } else 
    // 单个寄存器
    if (tokens[p].type == TK_REG) {
      // 读取寄存器值. 调用ISA层API: isa_reg_str2val(name, success)
      word_t reg_val = isa_reg_str2val(tokens[p].str + 1, success);
      if (!*success) {
        printf("Error: invalid register '%s'\n", tokens[p].str);
      }
      return reg_val;
    } else 
    //既不是数字也不是寄存器. 未知token.
    {
      printf("Error: invalid token type %d\n", tokens[p].type);
      *success = false;
      return 0;
    }
  }
  

  // 2.检查是否被括号包围
  if (check_parentheses(p, q)) {
    return eval_expr(p + 1, q - 1, success);
  }
  
  // 3.处理一元运算符 (TK_NEG 和 TK_DEREF)
  // 一元运算符已经在词法分析阶段标记好了，这里只需要简单判断
  // 一元运算符优先级高，只作用于紧邻的最小操作数
  if (p < q && (tokens[p].type == TK_NEG || tokens[p].type == TK_DEREF)) {
    // 找到一元运算符后面的最小操作数范围
    int operand_end = find_unary_operand_end(p + 1, q);
    if (operand_end == -1) {
      printf("Error: invalid operand for unary operator\n");
      *success = false;
      return 0;
    }
    
    // 递归求值操作数
    word_t operand_val = eval_expr(p + 1, operand_end, success);
    if (!*success) return 0;
    
    // 应用一元运算符
    word_t unary_result;
    if (tokens[p].type == TK_NEG) {
      unary_result = (word_t)(-(int)operand_val);
    } else {  // TK_DEREF
      unary_result = paddr_read(operand_val, 4);
    }
    
    // 如果一元运算符后面还有内容，需要继续处理
    if (operand_end < q) {
      // 例如: -a + b 或 *p + 1
      // 从 operand_end + 1 开始找主运算符
      int op = find_main_op(operand_end + 1, q);
      if (op == -1) {
        printf("Error: no operator found after unary expression\n");
        *success = false;
        return 0;
      }
      
      word_t val2 = eval_expr(op + 1, q, success);
      if (!*success) return 0;
      
      // 根据运算符计算
      switch (tokens[op].type) {
        case '+':    return unary_result + val2;
        case '-':    return unary_result - val2;
        case '*':    return unary_result * val2;
        case '/':
          if (val2 == 0) {
            printf("Error: division by zero\n");
            *success = false;
            return 0;
          }
          return unary_result / val2;
        case TK_EQ:  return unary_result == val2;
        case TK_NEQ: return unary_result != val2;
        case TK_AND: return unary_result && val2;
        case TK_OR:  return unary_result || val2;
        default:
          printf("Error: unknown operator %d\n", tokens[op].type);
          *success = false;
          return 0;
      }
    } else {
      // 只有一元运算符和其操作数
      return unary_result;
    }
  }
  
  // 找主运算符
  int op = find_main_op(p, q);
  if (op == -1) {
    printf("Error: no main operator found between %d and %d\n", p, q);
    *success = false;
    return 0;
  }
  
  // 递归求值左右子表达式
  word_t val1 = eval_expr(p, op - 1, success);
  if (!*success) return 0;
  
  word_t val2 = eval_expr(op + 1, q, success);
  if (!*success) return 0;
  
  // 根据运算符计算
  switch (tokens[op].type) {
    case '+':    return val1 + val2;
    case '-':    return val1 - val2;
    case '*':    return val1 * val2;
    case '/':    
      if (val2 == 0) {
        printf("Error: division by zero\n");
        *success = false;
        return 0;
      }
      return val1 / val2;
    case TK_EQ:  return val1 == val2;
    case TK_NEQ: return val1 != val2;
    case TK_AND: return val1 && val2;
    case TK_OR:  return val1 || val2;
    default:
      printf("Error: unknown operator %d\n", tokens[op].type);
      *success = false;
      return 0;
  }
}


// 最终的表达式求值函数. 包装两步函数:
//表达式解析为tokens: make_token()
//解析结果计算: eval_expr()
word_t expr(char *e, bool *success) {
  if (!make_token(e)) {
    *success = false;
    return 0;
  }

  // 调试: 打印所有识别出的 token
  print_tokens();

  // 使用完整的递归下降求值器
  *success = true;
  return eval_expr(0, nr_token - 1, success);
}
