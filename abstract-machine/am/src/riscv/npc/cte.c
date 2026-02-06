// cpu trap/exception handling
// 实现了中断/异常处理相关的函数.
#include <am.h>
#include <riscv/riscv.h>
#include <klib.h>

// Context结构体定义在riscv.h.
static Context* (*user_handler)(Event, Context*) = NULL;


// trap.S将CSR, GPR的当前快照 以Context结构体的组织 保存在stack上后, 将栈指针存在a0, 然后调用本函数.
// 本函数它的职责是把硬件异常状态翻译成软件事件(Event). 具体来说他需要实现:
/*
对mcause进行decode, (对mcause最高位判断是interrupt还是), 
分发到syscall, timer, external interrupt, page fault, illegal instruction...等事件.
*/ 
/* 那本函数啥时候从a0拿到指针作为参数呢? 参数搬运发生在 C → 汇编 的中间层: 回忆 RISCV ABI的调用约定:
| 寄存器   | 作用      |
| ----- | ------- |
| a0–a7 | 函数参数    |
| a0    | 第 1 个参数 |
| a0    | 返回值     |
所以在trap.S中的`call __am_irq_handle`等价于C中的`__am_irq_handle((Context*)a0)`
*/
Context* __am_irq_handle(Context *c) {
  if (user_handler) {
    Event ev = {0};   //Event定义在am.h
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


// __am_asm_trap在trap.S中定义.
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

// 执行ecall指令来让硬件自陷. riscv32手册规定, 硬件实现ecall指令, 必须: 把当前pc存入mepc, 设置mcause, 跳转到mtvec指向的地址(那里存着trap.S, 接下来执行trap.S)
// 我们将要在mtvec指向的内存位置放一段汇编, 它叫trap.S, 它
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
