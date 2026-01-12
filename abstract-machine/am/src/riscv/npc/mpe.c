// multi-processor emulation
// 该文件实现了多处理器相关的接口.
#include <am.h>

bool mpe_init(void (*entry)()) {
  return false;
}

int cpu_count() {
  return 1;
}

int cpu_current() {
  return 0;
}

int atomic_xchg(int *addr, int newval) {
  return 0;
}
