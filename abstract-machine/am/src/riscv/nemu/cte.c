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


extern void __am_asm_trap(void);  // __am_asm_trap在trap.S中定义. 职责是记录cpu快照, 保存为一个stack上的Context结构体, 并在a7写入指针.


/** 
cte_init函数, 给user_handler空函数句柄注册为handler函数. 用户程序需要"在事件发生时"调用它, 注册自己的事件处理函数.
* @param handler "用户程序"提供的事件处理函数指针. 中断/异常时, 函数__am_irq_handle()会调用这个handler函数来处理事件.
*                handler接收一个Event(事件类型)和Context(当前上下文), 返回下一个要切换到的Context.
*/
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


/*
以am-test中的yield test来分析内陷调用链条.

执行make ARCH=riscv32-nemu run mainargs=i后,

* main函数作为客户函数, 首先执行: `cte_init(simple_trap);` 来注册异常处理函数.

  * cte_init函数所做的: 令user_handler空函数句柄注册为传入的参数, 即simple_trap函数, 并把符号__am_asm_trap放入nemu的mtvec寄存器. 

* 然后main调用yield()函数.

* yield())会设置a7=-1并调用ecall指令.

* ecall指令所做的事(见inst.c)调用函数isa_raise_intr, 并把返回值赋给dnpc, 从而nemu下个周期准备跳转到mtvec寄存器值.

  * isa_raise_intr所做的事: 写mepc, mcause, mstatus寄存器, 并返回mtvec寄存器的值. 

* cte_init已经设置mtvec指向的地址是`__am_asm_trap`符号. 这个符号在trap.S中声明, 所以下个周期, nemu执行trap.S的__am_asm_trap函数.

* __am_asm_trap(trap.S中)执行的内容是:在stack上保存上下文后, 调用C函数`__am_irq_handle`.

* __am_irq_handle函数调用 注册的user_handler函数. 于是开始执行simple_trap.

  * 这个例子中, simple_trap在终端输出一个'y'(表示yield成功)然后返回当前上下文. 程序继续运行.
  * 在这个例子中, 客户程序main有一个while死循环, 不停调用yield(), 于是不停触发异常处理程序simple_trap, 在终端打印'y'.
*/


