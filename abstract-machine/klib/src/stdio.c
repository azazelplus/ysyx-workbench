#include <am.h>
#include <klib.h>
#include <klib-macros.h>
#include <stdarg.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

#define SUPPORT_FPU 0 //目前没有FPU, 无法进行浮点运算.

// putch()函数在nemu的对应架构`trm.c`中实现.



// printf函数通过复用vsprintf将可变参数转化为字符串, 然后用putch()函数将字符打印到串口.
int printf(const char *fmt, ...) {
  va_list ap;
  char buffer[1024]; //临时缓冲区, 存放格式化后的字符串.
  //va_start可以找到所有可变参数的原因: 当调用`printf(const char *fmt, ...)`时, 传入函数的所有参数在内存中连续排列.
  va_start(ap, fmt);
  int len = vsprintf(buffer, fmt, ap);
  
  //打印buffer到终端
  for (int i = 0; i < len; i++) {
    putch(buffer[i]);
  }

  va_end(ap);
  return len;
}




// va_list string print formatted, 可变参数列表 格式化 输出到字符串 函数. 返回值为写入的字符数(不包括结尾的'\0').
// 虽然不是glibc级别的vsprintf, 但是对于裸机环境我觉得已经很牛逼了.、
// out: 输出缓冲区; fmt: 格式字符串; ap: 可变参数列表
int vsprintf(char *out, const char *fmt, va_list ap) {
  char *p = out;  //输出指针, 指向out开头准备写.
  char tmp[256]; // 用于暂存转换后的数字字符串，足够大以容纳长数字
  
  // 遍历fmt格式字符串直到结尾'\0'
  while (*fmt) {
    if (*fmt != '%') {  //如果不是'%'字符, 是普通字符, 直接复制到out即可.
      *p++ = *fmt++;  //这种写法是先对*p赋值然后p自增. 等同于`*p = *fmt; p++; fmt++;`
      continue;
    }

    // 如果fmt中发现 %:
    fmt++;  //跳过 '%', 准备获取格式说明符

    //获取格式说明符(format specifier) 五个部分: 例如 `%010.3lf`, 第一个0是flag(空位补0), 10是width, .3是precision, l是length(double类型), f是specifier(folat)
    // 1. 获取标志 (Flags)
    // 目前只支持 '0' (补零)
    int zero_pad = 0; //是否补零标志位. 默认不补零.
    if (*fmt == '0') {
      zero_pad = 1;
      fmt++;
    }

    // 2. 获取宽度 (Width)
    int width = 0;
    //解析宽度数字
    while (*fmt >= '0' && *fmt <= '9') {
      width = width * 10 + (*fmt - '0');
      fmt++;
    }

    // 3. 获取精度 (Precision)
    int precision = -1; // -1 表示未指定
    // 当发现'.'时, 解析精度数字
    if (*fmt == '.') {
      fmt++;
      precision = 0;
      while (*fmt >= '0' && *fmt <= '9') {
        precision = precision * 10 + (*fmt - '0');
        fmt++;
      }
    }

    // 4. 获取长度修饰符 (Length) - 暂时忽略 (如 l, ll, h)
    // 简单跳过，但这可能在 64 位系统上导致截断问题，需注意
    if (*fmt == 'l' || *fmt == 'h') fmt++;
    if (*fmt == 'l') fmt++; // 支持 ll

    // 5. 获取格式符 (Specifier)
    switch (*fmt) {

      
      case 's': {
        const char *s = va_arg(ap, const char *); //从ap中获取下一个类型为 const char* 的参数.
        if (!s) //如果s为空指针, 将其填充为"(null)"
          s = "(null)";   

        int len = 0;  //准备计算字符串长度
        const char *t = s;  //临时指针t, 用来遍历字符串s
        while (*t++) len++; //遍历计算长度

        // 处理字符串精度截断
        if (precision >= 0 && len > precision) 
          len = precision;
        
        // 处理宽度对齐, 即多出来的用空格补. (目前仅支持右对齐)
        while (len < width--) 
          *p++ = ' ';

        // 将处理好的字符串搬运到out. 
        for (int i = 0; i < len; i++) 
          *p++ = *s++;
        break;
      }


      case 'd': {
        int val = va_arg(ap, int);
        int is_neg = 0;
        if (val < 0) {
          is_neg = 1;
          val = -val;
        }
        
        // 转换整数到 tmp
        int pos = 0;
        if (val == 0) tmp[pos++] = '0';
        else {
          while (val > 0) {
            tmp[pos++] = (val % 10) + '0';
            val /= 10;
          }
        }
        
        // 计算实际需要的字符数 (包括负号)
        int actual_len = pos + is_neg;
        // 计算填充量
        int pad_len = width - actual_len;
        
        // 填充
        if (!zero_pad) {
          while (pad_len-- > 0) *p++ = ' ';
        }
        
        // 输出负号
        if (is_neg) *p++ = '-';
        
        // 补零
        if (zero_pad) {
          while (pad_len-- > 0) *p++ = '0';
        }
        
        // 逆序输出数字
        while (pos > 0) *p++ = tmp[--pos];
        break;
      }

      case 'c': {
        // char 提升为 int
        char c = (char)va_arg(ap, int);
        while (width-- > 1) *p++ = (zero_pad ? '0' : ' ');
        *p++ = c;
        break;
      }

      case 'x': 


      case 'p': {
        unsigned int val = (unsigned int)va_arg(ap, unsigned int); // 注意: 指针在 64 位下可能被截断，这里简化处理
        if (*fmt == 'p') {
          *p++ = '0'; *p++ = 'x';
          // 指针通常是指 long/uintptr_t，这里为了简化假设是 32 位或截断显示
        }
        
        int pos = 0;
        if (val == 0) tmp[pos++] = '0';
        else {
          while (val > 0) {
            int digit = val % 16;
            tmp[pos++] = (digit < 10) ? (digit + '0') : (digit - 10 + 'a');
            val /= 16;
          }
        }
        
        int pad_len = width - pos;
        while (pad_len-- > 0) *p++ = (zero_pad ? '0' : ' ');
        while (pos > 0) *p++ = tmp[--pos];
        break;
      }

      // 简单支持 %f (不使用 double, 避免浮点库依赖)
      // 若必须要 double，请确保编译器支持软浮点
      // 这里为了演示，我们实现一个非常基础的定点模拟或者是简单的 float 打印
      case 'f': {
        // 注意：va_arg(ap, double) 获取 double。如果没有 FPU，这可能会生成无法链接的 FPU 指令或软浮点调用。
        // 这里尝试实现一个简单的版本。
      #if SUPPORT_FPU
        double val = va_arg(ap, double);
        if (val < 0) {
          *p++ = '-';
          val = -val;
        }
        
        int ip = (int)val; // 整数部分
        double fp = val - ip; // 小数部分
        
        // 打印整数部分
        int pos = 0;
        if (ip == 0) tmp[pos++] = '0';
        else {
          while (ip > 0) {
            tmp[pos++] = (ip % 10) + '0';
            ip /= 10;
          }
        }
        while (pos > 0) *p++ = tmp[--pos];
        
        // 打印小数部分 (默认精度 6)
        if (precision < 0) precision = 6;
        if (precision > 0) {
          *p++ = '.';
          for (int i = 0; i < precision; i++) {
            fp *= 10;
            int digit = (int)fp;
            *p++ = digit + '0';
            fp -= digit;
          }
        }
        break;
      #endif
      }


      default:
        *p++ = *fmt;
    }
    fmt++;
  }
  
  *p = '\0';
  return p - out;
}



// 将格式化输出写入字符串缓冲区.
// out: 输出缓冲区. sprintf打印出的字符串存在out.
// fmt: 格式字符串. 即形如"The value is %d and %s."的字符串.
// ...: 可变参数列表. 
// 例: sprintf(result_buffer, "The value is %d and %s.", test_int, test_str);
// 包装vsprintf实现.
int sprintf(char *out, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);          // 从 参数fmt紧接着后面的内存开始取可变参数.
  int n = vsprintf(out, fmt, ap); //用vsprintf
  va_end(ap);
  return n;
}

int snprintf(char *out, size_t n, const char *fmt, ...) {
  panic("Not implemented");
}

int vsnprintf(char *out, size_t n, const char *fmt, va_list ap) {
  panic("Not implemented");
}

#endif
