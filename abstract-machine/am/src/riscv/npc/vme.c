// vitrual memory emulator. 目前不支持虚拟内存, 相关函数都返回默认值. 后续会实现这些函数来支持虚拟内存.

#include <am.h>

bool vme_init(void* (*pgalloc_f)(int), void (*pgfree_f)(void*)) {
  return false;
}

void protect(AddrSpace *as) {
}

void unprotect(AddrSpace *as) {
}

void map(AddrSpace *as, void *va, void *pa, int prot) {
}

Context *ucontext(AddrSpace *as, Area kstack, void *entry) {
  return NULL;
}
