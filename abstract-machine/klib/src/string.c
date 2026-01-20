#include <klib.h>
#include <klib-macros.h>
#include <stdint.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

// 计算字符串s的长度, 不包括结尾`\0`. 返回值为长度.
size_t strlen(const char *s) {
  size_t len = 0;
  while (s[len] != '\0') {
    len++;
  }
  return len;
}

// 复制字符串src到dst, 包括结尾的`\0`. 返回值为dst.
char *strcpy(char *dst, const char *src) {
  char *d = dst;
  while ((*d = *src) != '\0') {
    d++;
    src++;
  }
  return dst;
}


// 复制字符串src到dst, 最多复制n个字符. 如果src长度小于n, 则在dst后补`\0`. 返回值为dst.
// 注意本库函数的问题: 
// 1. dst长度小于src长度时, 不会在dst结尾添加`\0`         [危险]
// 2. dst长度大于src长度时, 循环在dst多出来的位置添加'\0'  [低效]
char *strncpy(char *dst, const char *src, size_t n) {
  char *d = dst;
  size_t i;
  for (i = 0; i < n; i++) {
    if ((*d = *src) != '\0') {
      d++;
      src++;
    } else {
      // src 已到达 \0，继续用 \0 填充剩余的 n-i 个位置
      d++;
    }
  }
  return dst;
}

// 连接字符串src到dst的结尾. 返回值为dst.
// 手册: strcat的性能会随着dst变长越来越慢(因为移动到dst结尾更慢了), 实际使用时建议维护一个指向结尾的指针.
char *strcat(char *dst, const char *src) {
  char *d = dst;

  //移动到dst的结尾
  while(*d != '\0') d++;

  //复制src的内容到dst的结尾
  strcpy(d, src);
  return dst;
}


// 比较字符串s1和s2的字典序. 返回值: <0 if s1<s2; 0 if s1==s2; >0 if s1>s2.
int strcmp(const char *s1, const char *s2) {
while (*s1 != '\0' && *s1 == *s2) {
    s1++;
    s2++;
  }
  return (unsigned char)*s1 - (unsigned char)*s2;
}


// 比较字符串s1和s2的字典序，最多比较n个字符。 返回值: <0 if s1<s2; 0 if s1==s2; >0 if s1>s2.
int strncmp(const char *s1, const char *s2, size_t n) {
  while (n > 0 && *s1 != '\0' && *s1 == *s2) {
    s1++;
    s2++;
    n--;
  }
  if (n == 0) return 0;
  return (unsigned char)*s1 - (unsigned char)*s2;
}

// 将字符c(或者说1byte数据)复制到 指针s 所指向的内存区域的前n个byte中. 返回值为 指针s.
// 字符c选用int只是历史惯性: 库函数printf(%c, mychar)也是如此, 会做默认参数提升, 把char mychar提升为int, 再取低8bit解析.
// %c期望得到unsigned char. 试图将一个负数char给打印出字符会出错, 因为ascii码的范围是0~127, 而负数会被解释为大于127的值. 结果会显示�(replacement character).
void *memset(void *s, int c, size_t n) {
  // void指针不允许解引用, 不孕许下标(UB!!) 先将s强制转换为unsigned char指针.
  // char和unsigned char区别: 
  unsigned char *p = (unsigned char *)s;
  // 将c转换为unsigned char类型, 只保留低8bit.
  unsigned char ch = (unsigned char)c;
  for (size_t i = 0; i < n; i++) {
    p[i] = ch;
  }
  return s;
}

// 将内存区域src的前n个字节复制到内存区域dst中. 返回值为dst.
void *memmove(void *dst, const void *src, size_t n) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  
  if (d < s) {
    // 不重叠或 dst 在 src 之前, 从前往后复制
    for (size_t i = 0; i < n; i++) {
      d[i] = s[i];
    }
  } else if (d > s) {
    // dst 在 src 之后（可能重叠）, 从后往前复制
    for (size_t i = n; i > 0; i--) {
      d[i-1] = s[i-1];
    }
  }
  // d == s 时, 无需复制
  return dst;
}

// 将内存区域src的前n个字节复制到内存区域dst中. 返回值为dst.
void *memcpy(void *out, const void *in, size_t n) {
  unsigned char *d = (unsigned char *)out;
  const unsigned char *s = (const unsigned char *)in;
  for (size_t i = 0; i < n; i++) {
    d[i] = s[i];
  }
  return out;
}

// 比较内存区域s1和s2的前n个字节. 返回值: <0 if s1<s2; 0 if s1==s2; >0 if s1>s2.
int memcmp(const void *s1, const void *s2, size_t n) {
  const unsigned char *p1 = (const unsigned char *)s1;
  const unsigned char *p2 = (const unsigned char *)s2;
  for (size_t i = 0; i < n; i++) {
    if (p1[i] != p2[i]) {
      return p1[i] - p2[i];
    }
  }
  return 0;
}

#endif
