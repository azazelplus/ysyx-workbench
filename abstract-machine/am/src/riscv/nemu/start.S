.section entry, "ax"
.globl _start
.type _start, @function

_start:
  mv s0, zero
  la sp, _stack_pointer //把sp初始化为_stack_pointer的值. _stack_pointer定义在linker.ld: `_stack_top = ALIGN(0x1000); ` 具体位置所有代码/数据段大小）+ 对齐后的位置 + 0x8000, 预留了 32KB 的栈空间... 这就是给main函数用的栈了.
  call _trm_init

.size _start, . - _start
