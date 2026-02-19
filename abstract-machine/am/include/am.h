#ifndef AM_H__
#define AM_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include ARCH_H // this macro is defined in $CFLAGS
                // examples: "arch/x86-qemu.h", "arch/native.h", ...

// Memory protection flags
#define MMAP_NONE  0x00000000 // no access
#define MMAP_READ  0x00000001 // can read
#define MMAP_WRITE 0x00000002 // can write

// Memory area for [@start, @end)
typedef struct {
  void *start, *end;
} Area;

// Arch-dependent processor context。 是的, typedef也有前向声明的作用. 顺带办了. Context真正的定义在对应架构头文件.
typedef struct Context Context;

// Event是事件定义. 其中event是事件编号.
// An event of type @event, caused by @cause of pointer @ref
typedef struct {
  enum {
    EVENT_NULL = 0,
    EVENT_YIELD,      //yield, 主动让出cpu
    EVENT_SYSCALL,    //系统调用
    EVENT_PAGEFAULT,  //pagefault
    EVENT_ERROR,      //未分类的异常
    EVENT_IRQ_TIMER,  //定时器中断
    EVENT_IRQ_IODEV,  //外设中断
  } event;
  uintptr_t cause;    //
  uintptr_t ref;      //
  const char *msg;    //打印一些错误信息. 不是rv手册规范.
} Event;

// A protected address space with user memory @area
// and arch-dependent @ptr
typedef struct {
  int pgsize;
  Area area;
  void *ptr;
} AddrSpace;

#ifdef __cplusplus
extern "C" {
#endif

// ----------------------- TRM: Turing Machine -----------------------
extern   Area        heap;
void     putch       (char ch);
void     halt        (int code) __attribute__((__noreturn__));

// -------------------- IOE: Input/Output Devices --------------------
bool     ioe_init    (void);
void     ioe_read    (int reg, void *buf);
void     ioe_write   (int reg, void *buf);
#include "amdev.h"

// ---------- CTE: Interrupt Handling and Context Switching ----------
bool     cte_init    (Context *(*handler)(Event ev, Context *ctx));
void     yield       (void);
bool     ienabled    (void);
void     iset        (bool enable);
Context *kcontext    (Area kstack, void (*entry)(void *), void *arg);

// ----------------------- VME: Virtual Memory -----------------------
bool     vme_init    (void *(*pgalloc)(int), void (*pgfree)(void *));
void     protect     (AddrSpace *as);
void     unprotect   (AddrSpace *as);
void     map         (AddrSpace *as, void *vaddr, void *paddr, int prot);
Context *ucontext    (AddrSpace *as, Area kstack, void *entry);

// ---------------------- MPE: Multi-Processing ----------------------
bool     mpe_init    (void (*entry)());
int      cpu_count   (void);
int      cpu_current (void);
int      atomic_xchg (int *addr, int newval);

#ifdef __cplusplus
}
#endif

#endif
