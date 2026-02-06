// CTE (Context/Thread/Exception), 上下文切换与异常处理相关的接口实现
#include <am.h>
#include <riscv/riscv.h>
#include <klib.h>

static Context* (*user_handler)(Event, Context*) = NULL;

Context* __am_irq_handle(Context *c) {
  if (user_handler) {
    Event ev = {0};
    uintptr_t mcause = c->mcause;
    uintptr_t is_interrupt = (uintptr_t)1 << (__riscv_xlen - 1);

    if (mcause & is_interrupt) {
      switch (mcause & ~is_interrupt) {
        case 7: ev.event = EVENT_IRQ_TIMER; break;   // machine timer interrupt
        case 11: ev.event = EVENT_IRQ_IODEV; break;  // machine external interrupt
        default: ev.event = EVENT_ERROR; break;
      }
    } else {
      switch (mcause) {
        case 8:  // ecall from U-mode
        case 9:  // ecall from S-mode
        case 11: // ecall from M-mode
          if ((intptr_t)c->GPR1 == -1) ev.event = EVENT_YIELD;
          else ev.event = EVENT_SYSCALL;
          c->mepc += 4; // skip ecall
          break;
        default: ev.event = EVENT_ERROR; break;
      }
    }

    c = user_handler(ev, c);
    assert(c != NULL);
  }

  return c;
}

extern void __am_asm_trap(void);

bool cte_init(Context*(*handler)(Event, Context*)) {
  // initialize exception entry
  asm volatile("csrw mtvec, %0" : : "r"(__am_asm_trap));

  // register event handler
  user_handler = handler;

  return true;
}

Context *kcontext(Area kstack, void (*entry)(void *), void *arg) {
  return NULL;
}

void yield() {
#ifdef __riscv_e
  asm volatile("li a5, -1; ecall");
#else
  asm volatile("li a7, -1; ecall");
#endif
}

bool ienabled() {
  return false;
}

void iset(bool enable) {
}
