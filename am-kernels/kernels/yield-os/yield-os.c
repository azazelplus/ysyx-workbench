/**
 * yield-os - 一个简单的协作式多任务操作系统. 它是一个极简 OS 内核原型...
 * 
 * 这个程序演示了如何使用 AbstractMachine 的 CTE (上下文扩展) API
 * 来实现用户态的协作式多任务调度。两个任务会交替执行，分别输出 'A' 和 'B'。
 * 
 * - yield(): 主动让出 CPU，触发上下文切换

 * - schedule(): 调度器，决定下一个执行的任务
 */

#include <am.h>
#include <klib-macros.h>

#define STACK_SIZE (4096 * 8)  // 每个任务的栈大小: 32KB



typedef union {
  uint8_t stack[STACK_SIZE];  //给进程分配的(nemu虚拟内存内的)栈空间. stack[0]是数组起始地址, 也是栈顶. STACK_SIZE=32KB.
  struct { Context *cp; };  //匿名struct. 其实这里去掉`struct{}`直接写`Context *cp`效果是一样的. 可能为了兼容风格, 对其他架构来说PCB的定义更复杂吧...
} PCB;


static PCB pcb[2], pcb_boot, *current = &pcb_boot;



/**
 * f - 任务函数，两个用户函数的每个任务都执行这个函数.
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
 * @param ev:   触发调度的事件 (EVENT_YIELD)
 * @param prev: 指向当前任务被中断时的上下文
 * @return:     指向下一个要运行的任务的上下文
 */
static Context *schedule(Event ev, Context *prev) {
  current->cp = prev;     //将current->cp指向当前任务的上下文(prev). trap.S已经将寄存器状态写入了任务栈上(也就是prev指向的Context结构体), 这里
  current = (current == &pcb[0] ? &pcb[1] : &pcb[0]);   // 在两个任务之间切换 (轮转调度)
  return current->cp;   // 返回新任务的上下文
}


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

