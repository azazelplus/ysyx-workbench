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
decoder流程:
***************************************************************************************/

#ifndef __CPU_DECODE_H__
#define __CPU_DECODE_H__

#include <isa.h>

// Decode: 指令解码器状态结构体. 这里是顶层结构体, 不同isa
// vaddr_t即虚拟地址类型, = uint32_t = unsigned int.
typedef struct Decode {
  vaddr_t pc;   //当前指令的pc
  vaddr_t snpc; // static next pc 静态下一条指令的pc. = pc + 4.
  vaddr_t dnpc; // dynamic next pc 动态下一条指令的pc. 是真正的下一条PC. 可能因为跳转等原因和snpc不同.
  ISADecodeInfo isa;  // ISADecodeInfo结构体, 内部有一个变量uint32_t val, 保存当前指令的二进制编码.
  IFDEF(CONFIG_ITRACE, char logbuf[128]); // instruction trace 日志缓冲区.
} Decode;


// --- pattern matching mechanism ---
// 函数pattern_decode()的作用是, 对一个给定的由'0', '1', '?'组成的字符串pattern(比如U型指令对应pattern: ""??????? ????? ????? ??? ????? 00101 11")
// 注意: 它不直接写按指针传递的key, mask, shift变量, 而是先写入局部变量__key, __mask, __shift, 最后一次性写入key, mask, shift.
// 这种设计的好处是: 避免每次循环做内存写入, 性能更好. 
__attribute__((always_inline))  
static inline void pattern_decode
(
  const char *str, 
  int len,
  uint64_t *key, 
  uint64_t *mask, 
  uint64_t *shift
) 
{
  //
  uint64_t __key = 0, __mask = 0, __shift = 0;

// macro(i) 宏: 解析长度为len的字符串str的第i个字符. 把结果写入__key, __mask, __shift. 
// __key:                                            
// 它需要当前作用域有变量: len(字符串长度), str(要处理的字符串), __key(要写入的结果key), __mask(要写入的结果mask), __shift(要写入的结果shift).
// 例子: 想要解析"10?1"这个字符串. 要解析它, 我们要先定义变量:
// char str[] = "10?1"; //要解析的字符串
// int len = strlen(str);   // = 4
// uint32_t __key = 0;
// uint32_t __mask = 0;
// int __shift = 0;
// 现在调用macro(0); //解析str的第一个字符`1`, 然后调用macro(1), macro(2), macro(3)依次解析后续字符.   
#define macro(i) \
  if ((i) >= len) goto finish; \
  else { \
    char c = str[i]; \
    if (c != ' ') { \
      Assert(c == '0' || c == '1' || c == '?', \
          "invalid character '%c' in pattern string", c); \
      __key  = (__key  << 1) | (c == '1' ? 1 : 0); \
      __mask = (__mask << 1) | (c == '?' ? 0 : 1); \
      __shift = (c == '?' ? __shift + 1 : 0); \
    } \
  }

//递归展开macro(i) 64次...macro32(i)可以直接处理32bit和64bit的指令编码.
//这样做而不是for循环64次, 目的是让编译器在编译期展开优化成常数结果, 提高效率.
//编译器不敢轻易优化函数. 所以尽量写宏函数...
#define macro2(i)  macro(i);   macro((i) + 1)
#define macro4(i)  macro2(i);  macro2((i) + 2)
#define macro8(i)  macro4(i);  macro4((i) + 4)
#define macro16(i) macro8(i);  macro8((i) + 8)
#define macro32(i) macro16(i); macro16((i) + 16)
#define macro64(i) macro32(i); macro32((i) + 32)
  macro64(0);
  panic("pattern too long");
#undef macro
finish:
  *key = __key >> __shift;
  *mask = __mask >> __shift;
  *shift = __shift;
}




// 专门处理十六进制pattern字符串的解析函数. 例如"3f??".
// 和pattern_decode()类似, 只是每次处理4bit而不是1bit.
__attribute__((always_inline))
static inline void pattern_decode_hex(const char *str, int len,
    uint64_t *key, uint64_t *mask, uint64_t *shift) {
  uint64_t __key = 0, __mask = 0, __shift = 0;
#define macro(i) \
  if ((i) >= len) goto finish; \
  else { \
    char c = str[i]; \
    if (c != ' ') { \
      Assert((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '?', \
          "invalid character '%c' in pattern string", c); \
      __key  = (__key  << 4) | (c == '?' ? 0 : (c >= '0' && c <= '9') ? c - '0' : c - 'a' + 10); \
      __mask = (__mask << 4) | (c == '?' ? 0 : 0xf); \
      __shift = (c == '?' ? __shift + 4 : 0); \
    } \
  }

  macro16(0);
  panic("pattern too long");
#undef macro
finish:
  *key = __key >> __shift;
  *mask = __mask >> __shift;
  *shift = __shift;
}


// --- pattern matching wrappers for decode ---
// INSTPAT()宏用来匹配指令. 它生成 比较键key, 位掩码mask, shitf. 匹配成功后, 
// 它利用了两个宏: INSTPAT_INST(s)宏(提取Decode结构体s中的当前32bit指令); INSTPAT_MATCH宏(匹配指令模式并执行对应代码).
// 传入一个pattern字符串和可变参数`...`,  可变参数会被传给INSTPAT_MATCH宏.
//在 INSTPAT()宏中, 如果传入的pattern匹配成功, 识别为正确指令, 就会跳转到标签`goto *(__instpat_end);` 结束本条指令解码过程.
// `__instpat_end`是一个指向标签`__instpat_end__name`(其中name是用户传入的)的指针. `__instpat_end__name` 在INSTPAT_END(name)宏中定义.
#define INSTPAT(pattern, ...) do { \
  uint64_t key, mask, shift; \
  pattern_decode(pattern, STRLEN(pattern), &key, &mask, &shift); \
  if ((((uint64_t)INSTPAT_INST(s) >> shift) & mask) == key) { \
    INSTPAT_MATCH(s, ##__VA_ARGS__); \
    goto *(__instpat_end); \
  } \
} while (0)

// 关于宏的可变参数: MY_MACRO(arg, ...)这个参数宏使用了可变参数`...`. 它允许传入任意数量的参数(包括0个)作为宏的最后一个参数.
// 使用可变参数后, 在该宏体内, 你可以使用`##__VA_ARGS__`来引用它们. 而`##`是 C 的 token-pasting 运算符, 它的作用是: 如果`__VA_ARGS__`为空, 那么##会把前面的逗号`,`删掉, 等价于MY_MACRO(arg). , 这样让可变参数为空的时候语法仍然合法.




// INSTPAT_START(name) 和 INSTPAT_END(name) 是必须成对使用组成完整的代码块. 它们要和宏 INSTPAT() 一起使用.
// 前者的`{`需要后者的`}`来闭合, 中间填多个 INSTPAT()宏. 见函数 decode_exec() 中的使用.
// 它们仨的作用是进行跳转管理(其实等价于ifelse结构但是用宏和goto实现):
// INSTPAT_START(name) 定义了一个类型为`const void **`的指针, 叫做`__instpat_end`, 并将其初始化为标签 instpat_end_name 的地址.  
// INSTPAT_END(name) 声明了标签  `instpat_end_name`.
// 这对宏合起来就是先声明一个叫`__instpat_end`的指针, 指向标签`instpat_end_name`, 然后在结尾定义这个标签`instpat_end_name`, 在中间则是多段模式匹配, 每一段如果匹配成功就会goto最后的`instpat_end_name`, 离开这个代码块.
#define INSTPAT_START(name) { const void * __instpat_end = &&concat(__instpat_end_, name);

#define INSTPAT_END(name)   concat(__instpat_end_, name): ; }


// 关于标签的C语言处理问题:
// C语言约定, `label的指针`的数据类型是void *, 这里为啥多了一个`*`? 好吧, 其实你加10个*也可以, 甚至改成char *也可以. 只要__instpat_end是个指针, 它解引用一次`*__instpat_end`的结果就是标签地址, 不管你认为它是什麽类型的指针. 你总是可以goto *__instpat_end.
// 所以, 实际上 *(__instpat_end) 就是 instpat_end_name.  下面两个goto等价: `goto *(__instpat_end);`  `goto instpat_end_name;`
// concat(a, b)宏就是常用的a##b.

#endif
