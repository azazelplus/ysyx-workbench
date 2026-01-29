#ifndef KLIB_MACROS_H__
#define KLIB_MACROS_H__

#define ROUNDUP(a, sz)      ((((uintptr_t)a) + (sz) - 1) & ~((sz) - 1))
#define ROUNDDOWN(a, sz)    ((((uintptr_t)a)) & ~((sz) - 1))
#define LENGTH(arr)         (sizeof(arr) / sizeof((arr)[0]))

//RANGE宏: 用来把两个地址包装成一个Area结构体.
//例如 trm.c中堆区域的定义: Area heap = RANGE(&_heap_start, PMEM_END);
//展开就是:
/*
Area heap = (Area){
  .start = (void *)&_heap_start,
  .end   = (void *)PMEM_END
};
*/
#define RANGE(st, ed)       (Area) { .start = (void *)(st), .end = (void *)(ed) }

// IN_RANGE宏: 检查指针ptr是否在area定义的区域内.
#define IN_RANGE(ptr, area) ((area).start <= (ptr) && (ptr) < (area).end)

#define STRINGIFY(s)        #s
#define TOSTRING(s)         STRINGIFY(s)
#define _CONCAT(x, y)       x ## y
#define CONCAT(x, y)        _CONCAT(x, y)

#define putstr(s) \
  ({ for (const char *p = s; *p; p++) putch(*p); })

#define io_read(reg) \
  ({ reg##_T __io_param; \
    ioe_read(reg, &__io_param); \
    __io_param; })

#define io_write(reg, ...) \
  ({ reg##_T __io_param = (reg##_T) { __VA_ARGS__ }; \
    ioe_write(reg, &__io_param); })

#define static_assert(const_cond) \
  static char CONCAT(_static_assert_, __LINE__) [(const_cond) ? 1 : -1] __attribute__((unused))

#define panic_on(cond, s) \
  ({ if (cond) { \
      putstr("AM Panic: "); putstr(s); \
      putstr(" @ " __FILE__ ":" TOSTRING(__LINE__) "  \n"); \
      halt(1); \
    } })

#define panic(s) panic_on(1, s)

#endif
