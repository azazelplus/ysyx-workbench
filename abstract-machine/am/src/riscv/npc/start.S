

.section entry, "ax"
.globl _start
.type _start, @function

_start:
  mv s0, zero             # 将s0(riscv的保存寄存器)清零.
  la sp, _stack_pointer   # la=load address伪指令, 设置栈顶指针sp为_stack_pointer地址.
  call _trm_init          # 调用_trm_init函数, 这是C程序的入口.

.size _start, . - _start
