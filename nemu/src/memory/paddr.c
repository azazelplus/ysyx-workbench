/***************************************************************************************
* Copyright (c) 2014-2022 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
# 实现物理内存相关.
***************************************************************************************/

#include <memory/host.h>
#include <memory/paddr.h>
#include <device/mmio.h>
#include <isa.h>

// ============ MTRACE 配置 ============
// 如果在Kconfig中打开了MTRACE选项, 则CONFIG_MTRACE=1
#ifdef CONFIG_MTRACE
  #ifdef CONFIG_MTRACE_RANGE_ENABLE
    //定义一个 检查宏MTRACE_IN_RANGE(addr), 如果传入的addr在用户设置的[START, END)范围内则返回true
    #define MTRACE_IN_RANGE(addr) \
      ((addr) >= CONFIG_MTRACE_RANGE_START && (addr) < CONFIG_MTRACE_RANGE_END)
  #else
  // 如果没有开启地址范围过滤MTRACE_RANGE_ENABLE, 则所有地址都认为在范围内, 返回true
    #define MTRACE_IN_RANGE(addr) true
  #endif

  // 日志宏.
  #define MTRACE_LOG(type, addr, len, data) \
    do { \
      if (CONFIG_MTRACE_COND_EXPR && MTRACE_IN_RANGE(addr)) { \
        log_write("[mtrace] %s: addr=" FMT_PADDR ", len=%d, data=" FMT_WORD ", pc=" FMT_WORD "\n", \
                  type, addr, len, data, cpu.pc); \
      } \
    } while (0)
#else
  //如果没有开启MTRACE, 日志宏什麽也不做
  #define MTRACE_LOG(type, addr, len, data) ((void)0)
#endif




#if defined(CONFIG_PMEM_MALLOC)
static uint8_t *pmem = NULL;
#else // CONFIG_PMEM_GARRAY
static uint8_t pmem[CONFIG_MSIZE] PG_ALIGN = {};  //分配nemu的物理内存数组.
#endif



// guest_to_host是一个物理内存模拟器组件. 
//nemu的物理内存是一个数组: uint8_t pmem[CONFIG_MSIZE];
//它接收一个
uint8_t* guest_to_host(paddr_t paddr) { return pmem + paddr - CONFIG_MBASE; }
paddr_t host_to_guest(uint8_t *haddr) { return haddr - pmem + CONFIG_MBASE; }

static word_t pmem_read(paddr_t addr, int len) {
  word_t ret = host_read(guest_to_host(addr), len);
  return ret;
}

static void pmem_write(paddr_t addr, int len, word_t data) {
  host_write(guest_to_host(addr), len, data);
}

static void out_of_bound(paddr_t addr) {
  panic("address = " FMT_PADDR " is out of bound of pmem [" FMT_PADDR ", " FMT_PADDR "] at pc = " FMT_WORD,
      addr, PMEM_LEFT, PMEM_RIGHT, cpu.pc);
}

void init_mem() {
#if   defined(CONFIG_PMEM_MALLOC)
  pmem = malloc(CONFIG_MSIZE);
  assert(pmem);
#endif
  IFDEF(CONFIG_MEM_RANDOM, memset(pmem, rand(), CONFIG_MSIZE));
  Log("physical memory area [" FMT_PADDR ", " FMT_PADDR "]", PMEM_LEFT, PMEM_RIGHT);
}


// 读取物理地址. 其实就是包装了pmem_read实现. len是读取字节数, 可以为1,2,3,4. 例如addr=0x80000008, len=4, 读取4byte数据.
word_t paddr_read(paddr_t addr, int len) {
  word_t data = 0;  //读到的数据
  //判断addr是不是允许访问的有效物理内存地址
  if (likely(in_pmem(addr))) {
    data = pmem_read(addr, len);  //读取
    MTRACE_LOG("READ ", addr, len, data); //mtrace实现写日志(关闭mtrace的时候, 这个宏啥也不干)
    return data;
  }

  // mtrace实现: (如果关闭了CONFIG_DEVICE, 这一行宏展开就是啥也没有)
  IFDEF(CONFIG_DEVICE, data = mmio_read(addr, len); MTRACE_LOG("MMIO_R", addr, len, data); return data);

  out_of_bound(addr);
  return 0;
}

// 写内存地址. 例如addr=0x80000008, len=4, data=0x12345678, 将data的4byte写入addr开始的4byte内存.
void paddr_write(paddr_t addr, int len, word_t data) {
  MTRACE_LOG("WRITE", addr, len, data);
  if (likely(in_pmem(addr))) { pmem_write(addr, len, data); return; }
  IFDEF(CONFIG_DEVICE, MTRACE_LOG("MMIO_W", addr, len, data); mmio_write(addr, len, data); return);
  out_of_bound(addr);
}
