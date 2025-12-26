//实现上下文管理.
#include <am.h>
#include <loongarch/loongarch32r.h>
#include <klib.h>

static Context* (*user_handler)(Event, Context*) = NULL;

Context* __am_irq_handle(Context *c) {
  if (user_handler) {
    Event ev = {0};
    uintptr_t ecode = 0;
    switch (ccode) {
      default: ev.event = EVENT_ERROR; break;
    }

    c = user_handler(ev, c);
    assert(c != NULL);
  }

  return c;
}

extern void __am_asm_trap(void);

bool cte_init(Context*(*handler)(Event, Context*)) {
  // initialize exception entry
  asm volatile("csrwr %0, 0xc" : : "r"(__am_asm_trap));  // 0xc = eentry

  // register event handler
  user_handler = handler;

  return true;
}

//创建内核线程上下文, 返回值是上下文指针.
Context *kcontext(Area kstack, void (*entry)(void *), void *arg) {
  return NULL;
}

//自陷当前线程, 以便进入内核态.
void yield() {
  asm volatile("li.w $a7, -1; syscall 0");
}

//查询中断状态, 返回值表示中断是否被使能.
bool ienabled() {
  return false;
}

//设置中断状态为enable或disable.
void iset(bool enable) {
}
