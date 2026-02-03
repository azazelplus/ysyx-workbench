#include <am.h>
#include <klib.h>
#include <klib-macros.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)
static unsigned long int next = 1;


// nemu的.ld分配中, heap处于内存的末尾.也就是其他section分配完后的剩余位置起点(4KB对齐后)作为_heap_start.
// addr 充当堆的"break"指针(或程序断点).
// 它跟踪下一个空闲内存块的起始地址. 或者说, 它是堆顶.
// 初始为NULL, 首次调用malloc时将被设置为heap.start.
static void *addr = NULL;

int rand(void) {
  // RAND_MAX assumed to be 32767
  next = next * 1103515245 + 12345;
  return (unsigned int)(next/65536) % 32768;
}

void srand(unsigned int seed) {
  next = seed;
}

int abs(int x) {
  return (x < 0 ? -x : x);
}

int atoi(const char* nptr) {
  int x = 0;
  while (*nptr == ' ') { nptr ++; }
  while (*nptr >= '0' && *nptr <= '9') {
    x = x * 10 + *nptr - '0';
    nptr ++;
  }
  return x;
}


//申请 size Byte的内存.
void *malloc(size_t size) {
  // On native, malloc() will be called during initializaion of C runtime.
  // Therefore do not call panic() here, else it will yield a dead recursion:
  //   panic() -> putchar() -> (glibc) -> malloc() -> panic()
#if !(defined(__ISA_NATIVE__) && defined(__NATIVE_USE_KLIB__))
  if (addr == NULL) {addr = heap.start;}  //初次调用时, 将addr初始化为heap.start.
  
  size = (size_t)ROUNDUP(size, 8);  //ROUNDUP向上舍入到8字节对齐. 申请内存必须一次8Byte(64bit)对齐.
  void *old = addr; //记录当前堆顶
  addr = (void *)((uintptr_t)addr + size);  // 移动堆顶, 分配内存.

  //检查是否超过设定允许的堆区间.
  if ((uintptr_t)addr > (uintptr_t)heap.end) {return NULL;}

  //返回旧的堆底, 也就是开辟新堆空间的开始.
  return old;
#endif
  return NULL;
}


void free(void *ptr) {
}

#endif
