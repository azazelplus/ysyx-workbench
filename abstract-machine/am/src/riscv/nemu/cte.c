/*
CTE (Context/Thread/Exception), 上下文切换与异常处理相关的接口实现. am的一部分.
am代码提供运行时环境, 参与编译客户程序最后编译成的image文件. 
*/
#include <am.h>
#include <riscv/riscv.h>
#include <klib.h>

//声明一下user_handler函数指针, 它是cte的事件处理函数. 初始化为NULL, 用户程序有责任通过调用cte_init(用户提供的事件处理函数)来让user_handler=用户提供的事件处理函数, 这就叫完成了"用户处理函数注册". Context结构体在架构头文件am/include/arch/riscv.h中定义.
static Context* (*user_handler)(Event, Context*) = NULL;



// 调用时机: trap.S将CSR, GPR的当前快照 "以Context结构体的组织" 保存在stack上后, 将栈指针存在a0, 然后调用本函数.
// __am_irq_handle函数的职责是把硬件异常状态翻译成软件事件(Event). 具体来说他实现:
/*对mcause进行decode, (对mcause最高位判断是interrupt还是..), 分发到syscall, timer, external interrupt, page fault, illegal instruction...等事件. 其实就是把包含完整信息的mcause翻译, 得到Event分类结构体*/ 
Context* __am_irq_handle(Context *c) {
  if (user_handler) {
    Event ev = {0};                 // 初始化事件结构体. 这是个枚举类型, 描述是啥事件.
    uintptr_t mcause = c->mcause;   // 读取上下文的mcause寄存器值.
    uintptr_t mask = (uintptr_t)1 << (__riscv_xlen - 1);  //根据mcause最高位判断是中断还是异常. RISC-V规定, mcause最高位为1表示中断, 为0表示异常. 这里mask掩码=100...000 ,用来判断mcause的最高位.

    if (mcause & mask) {
      //如果是interrupt
      switch (mcause & ~mask) {
        case 7: {
          // TODO: handle machine timer interrupt if needed
          ev.event = EVENT_IRQ_TIMER;
          break;
        }
        case 11: {
          // TODO: handle machine external interrupt if needed
          ev.event = EVENT_IRQ_IODEV;
          break;
        }
        default: {
          // TODO: add more interrupt sources here when needed
          ev.event = EVENT_ERROR;
          break;
        }
      }
    } else {
      //如果是exception/ecall同步异常
      switch (mcause) {
        case 8:  // ecall from U-mode
        case 9:  // ecall from S-mode
        case 11: { // ecall from M-mode
          // TODO: split by privilege level if needed
          if ((intptr_t)c->GPR1 == -1) ev.event = EVENT_YIELD;
          else ev.event = EVENT_SYSCALL;
          c->mepc += 4; // 对于ecall异常, 需要mepc+4软件跳过. 否则mret回去又会执行一遍ecall, 陷入死循环.
          break;
        }
        default: {
          // TODO: add more exception types here when needed
          ev.event = EVENT_ERROR;
          break;
        }
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


/**
 * kcontext - 在指定栈空间(nemu模拟器拥有的虚拟内存的一段地址)创建内核线程的初始上下文, 并设置sp在栈顶.
 * 在指定的栈空间上创建一个新的上下文，使得当调度器(对yield-os就是schedule函数)切换到这个上下文时, CPU 会从 entry 函数开始执行，并传入 arg 作为参数。
 * @param kstack: 内核栈的地址范围 (Area.start = 栈底, Area.end = 栈顶, 是一个Area结构体(二元数组).)
 * @param entry:  线程入口函数指针
 * @param arg:    传递给入口函数的参数
 * @return:       指向新创建的上下文的指针
 * Context结构体在架构头文件 riscv.h
 * 1. 在栈顶预留一个 Context 结构体的空间
 * 2. 初始化关键寄存器：
 *    - mepc: 设为 entry，异常返回时会跳转到这里开始执行
 *    - mstatus: 设为 0x1800，表示 MPP=11 (Machine mode)
 *    - a0 (gpr[10]): 设为 arg，作为函数的第一个参数
 */
Context *kcontext(Area kstack, void (*entry)(void *), void *arg) {
  // 在栈顶(stack向下生长, kstack.end是地址最高位)预留 Context 结构体空间（栈从高地址向低地址增长）
  Context *c = (Context*)kstack.end - 1;  //指针减法, 减去一个Context结构体的大小, 让c指向这个预留的空间. 

  /**************************** 保存上下文 *****************************/
  c->mepc = (uintptr_t)entry;  // 设置程序计数器为入口函数地址
  c->mstatus = 0x1800;  // 设置机器态状态寄存器：0x1800 = 0b11000000000000, MPP (Machine Previous Privilege) 字段设为 11，表示返回到 Machine mode
  c->gpr[2] = (uintptr_t)c;  // 设置sp(x2是sp寄存器). 使用Context基址作为新线程栈顶, 保持与Context布局一致
  c->gpr[10] = (uintptr_t)arg;  // a0 = x10. 设置 a0 寄存器（ABI规定a0为第一个参数寄存器）为传入跳转函数的参数

  return c;
}
/*
* 举例: 我将要给0x80001000~0x80002000的内存空间创建一个riscv初始上下文, 我将运行:
kcontext((Area){0x80001000, 0x80002000}, entry, arg);
这将在下面范围nemu虚拟内存中写入4处:

          [ kstack.end ] (栈空间的最高处)
                │
                ▼
  0x80002000 ───┬───────────────────────────┐  <── kstack.end
                │      mepc (入口: entry)    │  ◄── c->mepc = (uintptr_t)entry;  // 设置程序计数器为入口函数地址
                ├───────────────────────────┤
                │     mstatus (状态: 0x1800) │  ◄── c->mstatus = 0x1800;  // 设置机器态状态寄存器：0x1800表示返回到 Machine mode 
                ├───────────────────────────┤
                │      ...(other regs)      │   ◄── 未初始化, 等trap.S负责保存.
                ├───────────────────────────┤
                │      ... (gpr[31..11])    │   ◄── 未初始化, 等trap.S负责保存.
                ├───────────────────────────┤
                │      a0 (参数: arg)        │  ◄── c->gpr[10] = (uintptr_t)arg 
                ├───────────────────────────┤
                │      ... (gpr[9..3])      │   ◄── 未初始化, 等trap.S负责保存.
                ├───────────────────────────┤
                │      sp (x2 寄存器值)      │  ◄── c->gpr[2] = c
                ├───────────────────────────┤
                │      ra (x1) / gpr[0]     │
返回的指针 c ──> └───────────────────────────┘  <── (Context*)kstack.end - 1 , 即栈顶, 即指针c, 即sp指针初值
                │                           │     
                │      可用栈空间 (Empty)    │
                │      (函数 f()运行的地方)  │
                │      (向下生长 ↓)          │
                │                           │
  0x80001000 ───┴───────────────────────────┘
          [ kstack.start ] (栈空间的底部)
*/



/**
 * yield - 主动让出 CPU，触发上下文切换.
 * 保存系统调用号在a7寄存器
 * 执行ecall指令
 */
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


/*************************************
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


