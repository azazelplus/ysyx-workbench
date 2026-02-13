// cpu trap/exception handling
// 实现了中断/异常处理相关的函数.
#include <am.h>
#include <riscv/riscv.h>
#include <klib.h>

/*
异常处理函数user_handler一开始是 NULL，需要用户函数主动通过 cte_init() 函数来注册一个提供的事件处理函数. 
工作流是这样的：
初始化阶段：程序启动时调用 cte_init(my_event_handler)
注册：cte_init 内部执行 user_handler = handler;，将用户传入的事件处理函数指针保存下来
运行时：当发生异常/中断时，__am_irq_handle 调用 注册的user_handler函数. 这就实现了跳转到异常处理函数.
*/ 
static Context* (*user_handler)(Event, Context*) = NULL;




// 调用时机: trap.S将CSR, GPR的当前快照 "以Context结构体的组织" 保存在stack上后, 将栈指针存在a0, 然后调用本函数.
// __am_irq_handle函数的职责是把硬件异常状态翻译成软件事件(Event). 具体来说他实现:
/*对mcause进行decode, (对mcause最高位判断是interrupt还是..), 分发到syscall, timer, external interrupt, page fault, illegal instruction...等事件. 其实就是把包含完整信息的mcause翻译, 得到Event分类结构体*/ 
/* 那本函数啥时候从a0拿到指针作为参数呢? 参数搬运发生在 C → 汇编 的中间层: 回忆 RISCV ABI的调用约定:
| 寄存器   | 作用      |
| ----- | ------- |
| a0–a7 | 函数参数    |
| a0    | 第 1 个参数 |
| a0    | 返回值     |
所以在trap.S中的`call __am_irq_handle`等价于C中的`__am_irq_handle((Context*)a0)`
*/
Context* __am_irq_handle(Context *c) {
  //确保有user_handler, 也就是用户注册了事件处理函数.
  if (user_handler) {
    Event ev = {0};                 //Event结构体定义在am.h
    uintptr_t mcause = c->mcause;   //提取mcause.
    uintptr_t mask = ((uintptr_t)1 << (__riscv_xlen - 1));  //根据mcause最高位判断是否是interrupt. 令掩码mask = 0x80000000, 与mcause按位与得到的结果如果不为0, 说明最高位是1, 是interrupt; 否则是exception.

    if (mcause & mask) {
      //如果是interrupt(外部中断). ~mask=32b01111..., 和macause相与得到mcause低31bit值
      switch (mcause & ~mask) {
        case 7: ev.event = EVENT_IRQ_TIMER; break;   // machine timer interrupt
        case 11: ev.event = EVENT_IRQ_IODEV; break;  // machine external interrupt
        default: ev.event = EVENT_ERROR; break;
      }
    } else {
      //如果是exception/ecall同步异常
      switch (mcause) {
        case 8:  
          // ecall from U-mode

        case 9:  
          // ecall from S-mode

        case 11: 
          // ecall from M-mode
          if ((intptr_t)c->GPR1 == -1) //检查系统调用号(RISCI即a7), -1代表yield.
            {ev.event = EVENT_YIELD;}  
          else  //其他调用号值代表"系统调用".
            {ev.event = EVENT_SYSCALL;}
          c->mepc += 4; // skip ecall. "系统调用"下, 硬件存的 mepc 指向的是 ecall 指令本身。如果不给它加 4，等会儿 mret 回去又会执行一遍 ecall，陷入无限死循环. 所以要手动跳过这一条
          break;
        default: ev.event = EVENT_ERROR; break; //默认: 未分类的异常.
      }
    }
    c = user_handler(ev, c);  //把解码Context结构体得到的ev结构体, 以及Context结构体本身, 上传给用户注册的事件处理函数, 让它来处理这个事件. 处理完后, 用户事件处理函数会返回一个新的Context结构体指针, 代表下一次要切换到的上下文. 这个Context结构体指针会被trap.S拿到, 然后切换到这个上下文继续执行.
    assert(c != NULL);  //
  }
  return c;
}


// __am_asm_trap在trap.S中定义. 职责是记录cpu快照, 保存为一个stack上的Context结构体, 并在a7写入指针.
extern void __am_asm_trap(void);


/** 
cte_init函数,
1. 执行汇编: `csrw mtvec, __am_asm_trap`, 即把符号__am_asm_trap放入mtvec寄存器.
2. 给user_handler空函数句柄注册为handler函数. 用户程序有责任调用它, 注册自己的事件处理函数.
* @param handler "用户程序"提供的事件处理函数指针. 中断/异常时, 函数__am_irq_handle()会调用这个handler函数来处理事件.
*                handler接收一个Event(事件类型)和Context(当前上下文), 返回下一个要切换到的Context.
*/
bool cte_init(Context*(*handler)(Event, Context*)) {
  // initialize exception entry
  asm volatile(
    "csrw mtvec, %0"            //汇编模板. %0表示第0个操作数
    :                           //输出操作数, 这里没有输出操作数, 所以留空
    : "r"(__am_asm_trap));      //输入操作数. "r"(expr)表示把expr(可以是一个符号)放入"某"个通用寄存器, 然后用这个寄存器replace模板中的%0

  // register event handler
  user_handler = handler;

  return true;
}

/** 
创建一个内核线程的上下文(Context结构体). k=kernel
这个Context包含了新线程开始执行所需的所有CPU状态信息(寄存器快照).
当使用这个Context进行上下文切换时, CPU会从entry函数开始执行, 并传入arg作为参数.

* @param kstack 内核栈的内存区域, 用Area结构体表示(包含start和end指针). 
*               Context结构体会被放置在这个栈的顶部(高地址端).
* @param entry  新线程的入口函数指针, 线程开始时会执行这个函数.
* @param arg    传递给entry函数的参数指针, 可以是任意类型的数据.
* @return       c指向新创建的Context结构体的指针. 这个Context可以用于后续的上下文切换.
*/
Context *kcontext(Area kstack, void (*entry)(void *), void *arg) {
  Context *c = (Context*)kstack.end - 1;

  c->mepc = (uintptr_t)entry;
  c->mstatus = 0x1800; // 0x1800 = 1100000000000. MPP=11(Machine Mode).
  c->GPR1 = (uintptr_t)arg; // a0

  return c;
}

/**
* yield()函数 会触发一个编号为EVENT_YIELD事件.
* tips: ABI规定, 对C函数的调用分为普通调用(function call, 使用`call myfun`指令触发)和系统调用(system call, 使用`ecall`指令触发)两种. 
  * 对function call, 参数传递从a0~a7; 
  * 对system call, 要求系统调用号放在a7中. 这里我们把系统调用号设为-1, 代表这是一个yield事件.
* yield首先往"系统调用号寄存器"(对RV32I来说ABI规定为a7)存入-1, 代表这是一个yield事件.
* 然后执行ecall指令来让硬件自陷. riscv32手册规定, 硬件实现ecall指令, 必须: 把当前pc存入mepc, 设置mcause, 跳转到mtvec指向的地址(那里存着trap.S, 接下来执行trap.S)
* 我们将要在mtvec指向的内存位置放一段汇编, 它叫trap.S, 它唤起ecall指令.
*/
void yield() {
#ifdef __riscv_e
  asm volatile("li a5, -1; ecall");
#else
  asm volatile("li a7, -1; ecall"); // 往a7里存一个-1, 然后调用ecall指令.
#endif
}


/**
* 下面这两个函数是针对外部中断的。它们的实现取决于具体的硬件平台和中断控制器。
* @param
*/
bool ienabled() {
  return false;
}

/**
* @param
*/
void iset(bool enable) {
}
