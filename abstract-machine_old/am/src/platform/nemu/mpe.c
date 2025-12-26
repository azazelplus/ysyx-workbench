#include <am.h>
#include <stdatomic.h>
#include <klib-macros.h>

bool mpe_init(void (*entry)()) {
  entry();
  panic("MPE entry returns");
}
//返回处理器数量
int cpu_count() {
  return 1;
}
//返回当前处理器编号
int cpu_current() {
  return 0;
}
//共享内存的原子交换操作
int atomic_xchg(int *addr, int newval) {
  return atomic_exchange(addr, newval);
}
