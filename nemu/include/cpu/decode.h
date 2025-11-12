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

#ifndef __CPU_DECODE_H__
#define __CPU_DECODE_H__

#include <isa.h>

typedef struct Decode {
  vaddr_t pc;
  vaddr_t snpc; // static next pc
  vaddr_t dnpc; // dynamic next pc
  ISADecodeInfo isa;
  IFDEF(CONFIG_ITRACE, char logbuf[128]);
} Decode;

// --- pattern matching mechanism ---
__attribute__((always_inline))
static inline void pattern_decode(const char *str, int len,
    uint64_t *key, uint64_t *mask, uint64_t *shift) {
  uint64_t __key = 0, __mask = 0, __shift = 0;

// macro(i) 宏: 解析长度为len的字符串str的第i个字符. 把结果写入__key, __mask, __shift. 
// 它需要当前作用域有变量: len(字符串长度), str(要处理的字符串), __key(要写入的结果key), __mask(要写入的结果mask), __shift(要写入的结果shift).
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


//递归展开macro(i) 64次...
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
// INSTPAT()宏传入一个pattern字符串和可变参数. 可变参数
//在 INSTPAT()宏中, 如果传入的pattern匹配成功, 识别为正确指令, 就会跳转到对应的标签处: `goto *(__instpat_end);`  
// `__instpat_end`是一个指向标签`__instpat_end__name`(其中name是用户传入的)的指针. `__instpat_end__name` 在INSTPAT_END(name)宏中定义.
#define INSTPAT(pattern, ...) do { \
  uint64_t key, mask, shift; \
  pattern_decode(pattern, STRLEN(pattern), &key, &mask, &shift); \
  if ((((uint64_t)INSTPAT_INST(s) >> shift) & mask) == key) { \
    INSTPAT_MATCH(s, ##__VA_ARGS__); \
    goto *(__instpat_end); \
  } \
} while (0)


// INSTPAT_START(name)和INSTPAT_END(name)必须成对使用组成完整的代码块:
// concat(a, b)宏的定义就是常用的a##b.
// const void ** __instpat_end = &&concat(__instpat_end_, name);     这一句定义了一个类型为`const void **`的指针, 叫做`__instpat_end`, 并将其初始化为标签 instpat_end_name 的地址.  
// 但是C语言约定, `label的指针`的数据类型是void *, 这里为啥多了一个`*`? 好吧, 其实你加10个*也可以, 甚至改成char *也可以. 只要__instpat_end是个指针, 它解引用一次`*__instpat_end`的结果就是标签地址, 不管你认为它是什麽类型的指针. 你总是可以goto *__instpat_end.
// 所以, 实际上 *(__instpat_end) 就是 instpat_end_name.  下面两个goto等价: `goto *(__instpat_end);`  `goto instpat_end_name;`
#define INSTPAT_START(name) { const void * __instpat_end = &&concat(__instpat_end_, name);

#define INSTPAT_END(name)   concat(__instpat_end_, name): ; }

#endif
