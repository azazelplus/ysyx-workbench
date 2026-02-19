/**
 * yield-os - 一个简单的协作式多任务操作系统. 它是一个极简 OS 内核原型...
 * 
 * 这个程序演示了如何使用 AbstractMachine 的 CTE (上下文扩展) API
 * 来实现用户态的协作式多任务调度。两个任务会交替执行，分别输出 'A' 和 'B'。
 * 
 * 核心概念：
 * - PCB (Process Control Block): 进程控制块，存储每个任务的上下文和栈
 * - yield(): 主动让出 CPU，触发上下文切换
 * - schedule(): 调度器，决定下一个执行的任务
 */

#include <am.h>
#include <klib-macros.h>

#define STACK_SIZE (4096 * 8)  // 每个任务的栈大小: 32KB


/**
 * PCB - Process Control Block (进程控制块)
 * 
 * 使用 union 来实现栈和上下文指针的组合：
 * - stack[]: 为任务分配的栈空间
 * - cp: 指向该任务当前保存的上下文 (Context). 初始化时配合 kcontext() 创建上下文并存储在 cp 中.

 例如: 如果我们初始化这样一个PCB:
  PCB pcb-test;
  pcb-test.cp = kcontext( Area{0x80001000, 0x80002000}, f, myarg );
 那麽就在地址 0x80001000~0x80002000 的内存空间上分配了一个进程栈. 它长这样:


高地址
0x80002000  ────────────────────────────  ← kstack.end (栈空间上界)
            |  Context 结构体 (cp)       |
            |  ------------------------ |
            |  gpr[0..31]               |
            |  mstatus                  |
            |  mepc                     |
            |  mcause                   |
0x80001F7C  ────────────────────────────   ← cp 指向这里. 初始时, sp=cp.
0x80001F78  |                           |
            |                           |
            |       可用栈空间           |
            |       (向下增长 ↓)         |
            |                           |
            |                           |
            |                           |
            |                           |
            |                           |
0x80001000  ────────────────────────────  ← stack[0]
低地址


初始时, sp= 

 */
typedef union {
  uint8_t stack[STACK_SIZE];  //给进程分配的(nemu虚拟内存内的)栈空间. stack[0]是数组起始地址, 也是栈顶. STACK_SIZE=32KB.
  struct { Context *cp; };  //匿名struct. 其实这里去掉`struct{}`直接写`Context *cp`效果是一样的. 可能为了兼容风格, 对其他架构来说PCB的定义更复杂吧...
} PCB;

/**
 * 全局变量PCB对象. 它们作为客户程序的全局变量, 首先编译进image的BSS/data段, 然后加载到nemu内存数组的固定位置, 不在任何物理栈(指的是nemu的视角看上去的在nemu内存空间的物理栈)上.
 * yield-os这个用户程序将pcb[n].stack[]这个数组当作第n个进程的栈空间来用. 这是在nemu来看利用一段nemu的内存的模拟栈空间. 这和main函数本身用的物理栈完全是nemu的内存空间的两个不同的部分.)
 * - pcb[2]: 两个用户任务的 PCB
 * - pcb_boot: 启动任务的 PCB. current被初始化为pcb_boot, 但是实际上目前的实践里它根本没用. 因为main 函数运行的栈是上电后运行的start.S中对sp操作分配的. 具体来说(详见start.S)就是`_stack_pointer`标签处.
 * - current: 指向当前正在运行的任务的 PCB
 nemu 启动时它们作为image的 BSS 段被清零加载到内存的固定位置:
  0x80000000  .text
  0x80000C00  .bss 段开始
              pcb[0]   占 32KB  → 地址 0x80000C00 ~ 0x80008C00
              pcb[1]   占 32KB  → 地址 0x80008C00 ~ 0x80010C00
              pcb_boot 占 32KB  → ...
              current  占 4B    → ...
 */
static PCB pcb[2], pcb_boot, *current = &pcb_boot;



/**
 * f - 任务函数，两个用户函数的每个任务都执行这个函数.
 * 
 * 这是一个永不返回的函数，作为每个任务的入口点。
 * 任务会循环执行以下操作：
 * 1. 输出一个字符 ('A' 或 'B'，由 arg 参数决定)
 * 2. 忙等待一段时间 (模拟任务工作)
 * 3. 调用 yield() 主动让出 CPU
 * 
 * @param arg: 任务参数
 *             - arg == 1: 输出 'A'
 *             - arg == 2: 输出 'B'
 *             - 其他值: 输出 '?'
 */
static void f(void *arg) {
  while (1) {
    // 根据 arg 的值输出不同字符
    putch("?AB"[(uintptr_t)arg > 2 ? 0 : (uintptr_t)arg]);
    
    // 忙等待，模拟任务执行一些工作
    for (int volatile i = 0; i < 100000; i++) ;
    
    // 主动让出 CPU，触发上下文切换
    yield();
  }
}

/**
 * schedule - 调度器函数，决定下一个运行的任务. 接收一个事件和当前任务的上下文，返回下一个要运行的任务的上下文.
 * 
 * yield-os中, 客户程序调用 yield() 时，会触发 CTE 的 EVENT_YIELD 事件，
 * CTE 会调用这个回调函数来选择下一个要运行的任务。
 * 
 * 调度策略：简单的轮转调度 (Round-Robin)
 * 1. 保存当前任务的上下文 (prev) 到 current->cp
 * 2. 切换 current 指针到另一个任务 (pcb[0] <-> pcb[1])
 * 3. 返回新任务的上下文，CTE 会恢复到该上下文继续执行
 * 
 * @param ev:   触发调度的事件 (EVENT_YIELD)
 * @param prev: 指向当前任务被中断时的上下文
 * @return:     指向下一个要运行的任务的上下文
 */
static Context *schedule(Event ev, Context *prev) {
  
  current->cp = prev;     //将current->cp指向当前任务的上下文(prev). trap.S已经将寄存器状态写入了任务栈上(也就是prev指向的Context结构体), 这里
  
  // 在两个任务之间切换 (轮转调度)
  current = (current == &pcb[0] ? &pcb[1] : &pcb[0]);
  
  // 返回新任务的上下文
  return current->cp;
}

/**
 * main - 主函数，初始化系统并启动多任务调度
 * 
 * 初始化流程：
 * 1. 调用 cte_init(schedule) 初始化 CTE (上下文扩展)，
 *    注册 schedule 作为事件处理回调函数
 * 
 * 2. 使用 kcontext() 为两个任务创建初始上下文：
 *    - pcb[0]: 执行 f(1)，会输出 'A'
 *    - pcb[1]: 执行 f(2)，会输出 'B'
 *    每个任务有独立的栈空间和入口函数
 * 
 * 3. 调用 yield() 触发第一次调度，从 pcb_boot 切换到 pcb[0]
 * 运行效果：屏幕会交替输出 'A' 和 'B'，两个任务协作式执行
 * 
 * @return: 理论上不会返回
 */
int main() {
  // 初始化 CTE，注册调度器
  cte_init(schedule);
  
  // 为任务 0 创建上下文. 传给kcontext的三个参数: 栈空间起始位置(Area结构体) + 入口函数 f + 传递给入口函数的参数 1
  pcb[0].cp = kcontext(
      (Area) {
        pcb[0].stack, 
        &pcb[0] + 1 
      },
      f,
      (void *)1L
    );
  
  // 为任务 1 创建上下文：栈空间 + 入口函数 f + 参数 2
  pcb[1].cp = kcontext(
    (Area) { 
      pcb[1].stack, 
      &pcb[1] + 1 
    }, 
    f, 
    (void *)2L
  );
  
  // 触发第一次调度，从启动上下文切换到任务 0
  yield();
  
  // 永远不应该执行到这里
  panic("Should not reach here!");
}


/******************运行yield-os的分析*******************
首先在am-kernels/kernels/yield-os目录下执行`make ARCH=riscv32-nemu run`,
这将:
* 编译好nemu可执行程序;
* 使用am代码(包括cte.c), 客户程序代码yield-os.c, 编译出客户image文件.
* 将image插入nemu可执行文件在linux内存中的nemu内存数组的位置, 然后运行nemu可执行程序.
  * 由linker.ld脚本规定, 
  * 这等价于在nemu模拟器上上执行`yield-os.c`中的main函数:

      int main() {
        cte_init(schedule); 
        pcb[0].cp = kcontext((Area) { pcb[0].stack, &pcb[0] + 1 }, f, (void *)1L);
        pcb[1].cp = kcontext((Area) { pcb[1].stack, &pcb[1] + 1 }, f, (void *)2L);
        yield();
        panic("Should not reach here!");
      }
* main函数的第一句就是`cte_init(schedule);`, 它:
  * 将yield-os.c中定义的事件处理函数`schedule`注册为(作为右值赋值)CTE.c中的全局变量user_handler函数.
  * 将符号`__am_asm_trap`的地址写入nemu的mtvec寄存器. 这符号在trap.S中声明, 
* 然后main函数将全局变量pcb[0]和pcb[1]的cp成员(即上下文指针)用kcontext函数初始化. kcontext函数将填入少量初始值(mepc/mstatus/sp/a0), 然后返回栈顶指针.

* 运行yield()函数, 它其实就是2条汇编指令: `li a7, -1; ecall`
  * nemu处理器执行li a7, -1指令, 将a7寄存器设置为-1, 这就是我们约定的系统调用号, 表示这次中断是一个yield系统调用.
  * nemu处理器执行ecall指令. 这将让nemu将下一周期pc指针dnpc指向函数`isa_raise_intr`函数(nemu的中断处理函数). 于是真机运行isa_raise_intr函数, nemu准备上下文, 准备跳转到mtvec寄存器指向的nemu虚拟地址, 也就是__am_asm_trap.
* nemu在下个周期开始运行__am_asm_trap函数. 它:
 * 在stack上保存gpr等寄存器上下文.
 * 调用cte.c中的C函数`__am_irq_handle`. 对于ecall触发中断, __am_irq_handle会:
  * 令mepc+=4(跳过ecall指令), 
  * 调用之前注册的事件处理函数schedule.(即`c = user_handler(ev, c);`)
* 于是nemu开始执行schedule事件处理函数. schedule函数会:
  * 将当前任务的上下文(它是栈区的一个Context结构体)保存到current->cp中.  //我从这里开始不懂了!!!
  * 切换current指针到另一个任务.
  * 返回新任务的上下文, 这时__am_irq_handle函数会把这个新上下文装入a0寄存器, 然后返回.
  
  
  
*********************************************************
*/



/* 关键节点的汇编全景
# ① yield() 的两条指令
800002f4 <yield>:
  li  a7, -1       # 放系统调用号 -1
  ecall            # NEMU 执行此指令: 写 mcause/mepc/mstatus, 令 dnpc = mtvec = 0x80000300
  ret              # ← 永远不会执行到这里，ecall 之后 PC 已经跳走了

# ② __am_asm_trap: 保存现场 (144 字节 = 36×4 = 32gpr+3csr+1pdir槽)
80000300: addi sp, sp, -144    # ← 开辟 Context 空间，sp 下移
80000304: sw   ra,   4(sp)
  ... 保存 x1~x31 全部 GPR ...
8000037c: csrr t0, mcause
80000380: csrr t1, mstatus
80000384: csrr t2, mepc
80000388~390: sw  t0/t1/t2, 128/132/136(sp)  # 保存三个 CSR

# ③ 把 Context* 作为参数，调用 C 函数
800003a0: mv   a0, sp                         # a0 = 当前任务的 Context*
800003a4: jal  ra, __am_irq_handle            # 进入 C 世界

          # __am_irq_handle 内:
          #   mepc += 4  (跳过 ecall)
          #   c = schedule(ev, c)
          #     → current->cp = prev          (保存旧任务 Context*)
          #     → current = pcb[1]            (切换 current 指针)
          #     → return pcb[1]->cp           (返回新任务 Context*，装进 a0)
          #   return c  (a0 = 新任务的 Context* = 0x80009xxx)

# ④ 上下文切换的"魔法"：就这一条指令
800003a8: mv   sp, a0    # ← sp 从旧任务的栈顶，直接切换到新任务的 Context* 地址
                         #    此后所有 lw 都从新任务的 Context 结构体里读

# ⑤ 从新 Context 恢复寄存器
800003ac: lw   t1, 132(sp)   # 读新任务的 mstatus
800003b0: lw   t2, 136(sp)   # 读新任务的 mepc（= 新任务 f 函数的入口地址）
800003b4: csrw mstatus, t1
800003b8: csrw mepc, t2
  ... 恢复 x1~x31 全部 GPR ...
80000434: addi sp, sp, 144    # 归还 Context 空间，sp 回到新任务可用栈顶

# ⑥ 返回新任务
80000438: mret     # NEMU 执行此指令: 令 dnpc = mepc（= 新任务 f 的入口）
                   # 下一条取到的指令就是新任务 f() 里的第一条
*/





/*
栈顶=sp. riscv的ABI规定:Full Descending. 也就是说, stack向下生长, 且sp指针指向[最后一次 push分配出的那块内存的起始地址], 也就是说sp指针地址处已经有数据了.  
每push一次, sp变小(sp=sp-size).

栈是全自动的, 编译器帮你管理sp. 

堆是全手动的. 你需要主动new/malloc. 和sp寄存器没有关系.


*/


/*
编译器的prologue:
在进入一个函数的开头插入的一段汇编代码. 它要完成的任务是开辟空间:
* `addi sp, sp, -size`  把栈指针往下挪, 在栈上挖出一个坑(stack frame)
* `sw ra, `


编译器的epilogue:
* 从stack frame内存区域读取ra
* `addi sp, sp, size`  把栈指针往上挪, 归还栈空间.




*/
