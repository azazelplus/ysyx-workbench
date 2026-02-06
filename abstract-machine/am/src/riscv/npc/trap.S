/*trap.S instruction segment is stored at addr saved in mtvec reg. This code is executed when trap is triggered.*/
/*WHAT `trap.S` DOES: save cpu's current GPR and CSR onto stack; call the exception handling function; restore the regs; return(using inst `mret`). */

/*C pre-processor section*/
#define concat_temp(x, y) x ## y
#define concat(x, y) concat_temp(x, y)/*to prevent expansion order issues of nested macros... wrap again.*/
#define MAP(c, f) c(f)

/*wrap riscv32's and riscv64's load&store inst*/
#if __riscv_xlen == 32
      #define LOAD  lw
      #define STORE sw
      #define XLEN  4
#else
      #define LOAD  ld
      #define STORE sd
      #define XLEN  8
#endif


/*thers's no f(2). cause `x2` reg is `sp`, stack pointer, need to treat specially...*/
#define REGS_LO16(f) \
      f( 1)       f( 3) f( 4) f( 5) f( 6) f( 7) f( 8) f( 9) \
f(10) f(11) f(12) f(13) f(14) f(15)
#ifndef __riscv_e
      #define REGS_HI16(f) \
                                          f(16) f(17) f(18) f(19) \
      f(20) f(21) f(22) f(23) f(24) f(25) f(26) f(27) f(28) f(29) \
      f(30) f(31)
      #define NR_REGS 32
#else
      #define REGS_HI16(f) /*empty implementation*/
      #define NR_REGS 16
#endif

/*wrap REGS_LO16 and REGS HI16. just use REGS() to */
#define REGS(f) REGS_LO16(f) REGS_HI16(f)

/*write GPR to stack and read GPR from*/
/*wrap inst `lw/ld/sw/sd xn, n*(32/64)(sp)` to `PUSH/POP(n)`.*/
/*example(risc32I):  PUSH(3) == lw x3 3*32(sp)              */
#define PUSH(n) STORE concat(x, n), (n * XLEN)(sp);
#define POP(n)  LOAD  concat(x, n), (n * XLEN)(sp);

/*size of context struct `Context` */
#define CONTEXT_SIZE  ((NR_REGS + 3 + 1) * XLEN)
#define OFFSET_SP     ( 2 * XLEN)
#define OFFSET_CAUSE  ((NR_REGS + 0) * XLEN)
#define OFFSET_STATUS ((NR_REGS + 1) * XLEN)
#define OFFSET_EPC    ((NR_REGS + 2) * XLEN)

/*`.align n` means: Align the current LC(location counter) to 2^n-byte boundary, by inserting padding.*/
.align 3
/* `.global foo` exports the symbol `foo` to global symbol table.*/
.globl __am_asm_trap


/*riscv inst part*/
__am_asm_trap:

/*First, move sp down to allocate stack space.*/
  addi sp, sp, -CONTEXT_SIZE
  
/* PUSH(REGS). sw insts. */
  MAP(REGS, PUSH)

/*csrr is pseudo inst. read CSR[csr], store the result ro rd. `csrr rd, csr` == `csrrs rd, csr, x0` */
/* read mcause, result to t0*/
  csrr t0, mcause
/*read mstatus, result to t1*/
  csrr t1, mstatus
/*read mepc, result to t2.*/
  csrr t2, mepc

/*store t0, t1, t2 to the stack.*/
  STORE t0, OFFSET_CAUSE(sp)
  STORE t1, OFFSET_STATUS(sp)
  STORE t2, OFFSET_EPC(sp)

/*let a0 = ap. That is, give the sp(that is, the ptr to struct `Context`) to a0.*/
/*At this point, we completes the task of storing GPR and CSR snapshots on the stack.*/
  mv a0, sp

/*call the C function __am_irq_handle() in cte.c*/
  call __am_irq_handle

/**/
  LOAD t1, OFFSET_STATUS(sp)
  LOAD t2, OFFSET_EPC(sp)
/**/
  csrw mstatus, t1
  csrw mepc, t2

  MAP(REGS, POP)

  addi sp, sp, CONTEXT_SIZE/*special treatment for sp*/
  mret
