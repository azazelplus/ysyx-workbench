src/memory/ 包含物理内存和虚拟内存的访问接口。

## 物理内存 (paddr.c)

### 核心函数

| 函数 | 说明 | 示例 |
|------|------|------|
| `paddr_read(addr, len)` | 读物理地址 `addr`，读取 `len` 字节 | `paddr_read(0x80000000, 4)` |
| `paddr_write(addr, len, data)` | 写物理地址 `addr`，写入 `len` 字节 | `paddr_write(0x80000000, 4, 0x12345678)` |
| `init_mem()` | 初始化物理内存（程序启动时调用） | |

### 内存转换函数

| 函数 | 说明 |
|------|------|
| `guest_to_host(paddr)` | 将客户机物理地址转换为主机内存指针 |
| `host_to_guest(haddr)` | 将主机内存指针转换回客户机物理地址 |
| `in_pmem(addr)` | 检查地址 `addr` 是否在有效物理内存范围内 |

### 内存范围宏

| 宏 | 说明 |
|----|------|
| `PMEM_LEFT` | 物理内存起始地址（= CONFIG_MBASE） |
| `PMEM_RIGHT` | 物理内存结束地址 |
| `RESET_VECTOR` | 程序执行起始地址 |

### 使用示例

```c
// 初始化内存系统
init_mem();

// 读取 4 字节数据
word_t val = paddr_read(0x80000000, 4);

// 写入 2 字节数据
paddr_write(0x80000004, 2, 0x1234);

// 检查地址是否有效
if (in_pmem(0x80000000)) {
  printf("Valid physical address\n");
}

// 地址转换
uint8_t *host_addr = guest_to_host(0x80000000);
paddr_t guest_addr = host_to_guest(host_addr);
```

## 虚拟内存 (vaddr.c)

### 核心函数

| 函数 | 说明 | 示例 |
|------|------|------|
| `vaddr_ifetch(addr, len)` | 获取指令（从虚拟地址） | `vaddr_ifetch(pc, 4)` |
| `vaddr_read(addr, len)` | 读虚拟地址 `addr`，读取 `len` 字节 | `vaddr_read(sp, 4)` |
| `vaddr_write(addr, len, data)` | 写虚拟地址 `addr`，写入 `len` 字节 | `vaddr_write(sp, 4, 0xdeadbeef)` |

### 分页相关宏

| 宏 | 说明 |
|----|------|
| `PAGE_SHIFT` | 页大小的位移（通常为 12，即 4KB） |
| `PAGE_SIZE` | 页大小（1 << PAGE_SHIFT） |
| `PAGE_MASK` | 页内偏移掩码 |

### 使用示例

```c
// 获取指令
word_t instr = vaddr_ifetch(cpu.pc, 4);

// 读栈上的数据
word_t stack_val = vaddr_read(cpu.sp, 4);

// 写栈
vaddr_write(cpu.sp - 4, 4, return_addr);

// 页对齐计算
paddr_t page_start = addr & ~PAGE_MASK;
paddr_t page_end = page_start + PAGE_SIZE;
```

## MMIO (内存映射 I/O)

当访问地址不在物理内存范围内时，会尝试 MMIO 操作（需要启用 CONFIG_DEVICE）。









