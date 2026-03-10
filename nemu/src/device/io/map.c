/***************************************************************************************
 * MMIO 内存空间管理
 * 
 * NEMU 中所有外设（串口、计时器、GPU 等）的寄存器都通过 MMIO（Memory-Mapped I/O）
 * 方式访问。CPU 通过读写指定的物理地址来与外设通信。
 * 
 * 这个模块管理一个统一的 IO 空间，为不同的外设分配虚拟内存区域：
 * - io_space: MMIO 空间的基址（malloc 分配的连续内存）
 * - p_space: 指向当前未被占用的位置（用于分配新外设的空间）
 * 
 * 例如：
 *   serial:  [0xa00003f8, 0xa00003ff] -> io_space 的某个位置
 *   rtc:     [0xa0000048, 0xa000004f] -> io_space 的另一个位置
 *   gpu:     [0xa0000100, 0xa0000107] -> io_space 的再另一个位置
***************************************************************************************/

#include <isa.h>
#include <memory/host.h>
#include <memory/vaddr.h>
#include <device/map.h>
#include <utils.h>

#define IO_SPACE_MAX (2 * 1024 * 1024)

// DTRACE 条件宏定义. 这里的CONFIG_DTRACE直接来自Kconfig生成, 而`CONFIG_DTRACE_COND_EXPR`首先是Kconfig生成的"CONFIG_DTRACE_COND", 然后在nemu/Makefile中脱去括号变成宏(gcc的-D选项注入宏)CONFIG_DTRACE_COND.
#ifdef CONFIG_DTRACE
  #define DTRACE_COND CONFIG_DTRACE_COND_EXPR
#else
  #define DTRACE_COND false
#endif

/***************************************************************************************
 * io_space p_space两个全局指针. 分别记录"总基址"和"当前分配位置". 
 * 
 * 【io_space：IO 空间的总基址】
 * - 值：malloc 返回的 2MB 内存的首地址
 * - 职责：记录整个 IO 空间在内存中的起始位置，永远不变
 * - 初始化时机：init_map() 时设置一次，之后不再改变
 * 
 * 【p_space：IO 空间的当前分配指针】
 * - 初始值：等于 io_space（指向 IO 空间的起始位置）
 * - 职责：记录下一个外设应该从哪里开始分配空间
 * - 初始化时机：init_map() 时设置一次
 * - 如何变化：每次调用 new_space(size) 时，p_space += page_aligned_size
 * - 用途：
 *   1. 跟踪已分配空间到哪里了
 *   2. 为新外设分配连续内存
 *   3. 实现简单的内存池（无需 free，只有单向分配）
 *
 * - io_space 保持不变，便于计算相对位置
 * - p_space 动态变化，便于跟踪分配进度
 * - 配合使用可以：
 *   * 检查是否超过限制：assert(p_space - io_space < IO_SPACE_MAX)
 *   * 实现简单快速的线性分配算法
 *   * 避免碎片化（因为总是从 p_space 继续分配）
 ***************************************************************************************/
//这两个全局指针在init_map()中初始化为malloc分配的IO空间, 初始相等.
static uint8_t *io_space = NULL;  //IO 空间的总基址（malloc 分配的 2MB 内存起始地址）
static uint8_t *p_space = NULL;   //IO 空间的当前分配指针（指向下一个可用位置）



/***************************************************************************************
 * new_space - 分配新的堆空间. 其实干的事情就是移动p_space, 堆全局指针.
 * 
 * 【分配过程示例】
 * 初始状态：
 *   io_space = 0x55000000  (malloc 分配的基址)
 *   p_space  = 0x55000000  (初始等于 io_space)
 *   已使用：0 字节
 * 
 * new_space(8) 调用 1：
 *   p = p_space = 0x55000000
 *   size → 4096
 *   p_space = 0x55000000 + 4096 = 0x55001000
 *   return 0x55000000  ← serial 设备的寄存器在这里
 * 
 * new_space(8) 调用 2：
 *   p = p_space = 0x55001000
 *   size → 4096
 *   p_space = 0x55001000 + 4096 = 0x55002000
 *   return 0x55001000  ← rtc 设备的寄存器在这里
 * 
 * new_space(8) 调用 3：
 *   p = p_space = 0x55002000
 *   size → 4096
 *   p_space = 0x55002000 + 4096 = 0x55003000
 *   return 0x55002000  ← gpu 设备的寄存器在这里
 * 
 * 最终状态：
 *   io_space = 0x55000000  (不变)
 *   p_space  = 0x55003000  (已分配了 12KB)
 *   已使用：p_space - io_space = 0x3000 = 12KB
 * 【参数说明】
 * @param size: 请求分配的大小（字节）
 *              实际分配会对齐到 PAGE_SIZE（通常 4096）
 * 【返回值】
 * 返回分配内存的起始地址（uint8_t *）
 * 【关键公式】
 * - 对齐计算：aligned_size = (size + PAGE_SIZE - 1) & ~PAGE_MASK
 *   等价于：aligned_size = ceil(size / PAGE_SIZE) * PAGE_SIZE
 * - 已使用空间：used = p_space - io_space
 * - 剩余空间：remain = IO_SPACE_MAX - (p_space - io_space)
 ***************************************************************************************/
uint8_t* new_space(int size) {
  uint8_t *p = p_space; //记录当前分配的内存起始地址.
  // page aligned;
  size = (size + (PAGE_SIZE - 1)) & ~PAGE_MASK; //size向上取整到页大小(PAGE_SIZE=4096)的整数倍. `& ~PAGE_MASK`将低位抹零.
  p_space += size;  //移动p_space全局指针, 分配内存.
  assert(p_space - io_space < IO_SPACE_MAX);    //确保没有超出IO空间的最大限制.
  return p; 
}


/***************************************************************************************
 * check_bound - 检查某个设备 map 能否访问某个内存 addr.
 * 【执行流程】
 * 1. 如果 map 为 NULL（没有为该地址注册任何设备），报错
 * 2. 检查地址是否在 [map->low, map->high] 范围内
 * 3. 如果越界，报错并显示设备名称和允许范围
 * 【参数说明】
 * @param map:  设备的 IOMap 结构. 包含该设备注册的地址范围.
 * @param addr: 要访问的物理地址
 ***************************************************************************************/
static void check_bound(IOMap *map, paddr_t addr) {
  //如果map==NULL, 传入不是注册设备, 直接报错.
  if (map == NULL) {
    Assert(map != NULL, "address (" FMT_PADDR ") is out of bound at pc = " FMT_WORD, addr, cpu.pc);
  } else {
    Assert(addr <= map->high && addr >= map->low,
        "address (" FMT_PADDR ") is out of bound {%s} [" FMT_PADDR ", " FMT_PADDR "] at pc = " FMT_WORD,
        addr, map->name, map->low, map->high, cpu.pc);
  }
}

/***************************************************************************************
 * invoke_callback - 调用外设的 IO 处理函数, 其实就是包装了一下设备的IO回调函数.
 * 【参数说明】
 * @param c:        IO设备 的 回调函数指针（可以为 NULL）
 * @param offset:   相对于设备基址的偏移
 * @param len:      访问长度（1、2、4 或 8 字节）
 * @param is_write: true = 写操作，false = 读操作
 * 
 * 【设备处理函数的职责】
 * 例如 timer.c 中的 rtc_io_handler:
 * - 当 is_write=false 且 offset=4 时，从 NEMU 获取当前时间更新寄存器
 * - 让 CPU 读到最新的时间值
 ***************************************************************************************/
static void invoke_callback(io_callback_t c, paddr_t offset, int len, bool is_write) {
  if (c != NULL) { c(offset, len, is_write); }  //当回调函数不为 NULL 时, 调用它. 否则为空操作.
}


/***************************************************************************************
 * init_map - 初始化 MMIO 的内存空间. malloc分配2MB内存在linux宿主机上.
 ***************************************************************************************/
void init_map() {
  io_space = malloc(IO_SPACE_MAX);  //malloc分配2MB内存作为IO空间, 初始化io_space指向这块空间.
  assert(io_space); //确保分配成功.
  p_space = io_space; //初始化全局变量p_space和io_space相等.
}



/***************************************************************************************
 * map_read - 从 MMIO 地址读取数据, 同时调用设备的回调函数.
 * 
 * 【参数说明】
 * @param addr: 要读取的物理地址
 * @param len:  读取长度（1、2、4 或 8 字节）
 * @param map:  设备的 IOMap 结构（包含基址、大小、处理函数等）
 * 
 * 【返回值】
 * 读到的数据（word_t 类型，通常是 64 位整数）
 ***************************************************************************************/
word_t map_read(paddr_t addr, int len, IOMap *map) {
  assert(len >= 1 && len <= 8); //读取长度是否有效
  check_bound(map, addr); //读取addr在设备允许范围内
  paddr_t offset = addr - map->low;
  invoke_callback(map->callback, offset, len, false); // prepare data to read
  word_t ret = host_read(map->space + offset, len);
  // Device trace: 记录设备读操作
  IFDEF(CONFIG_DTRACE,
    if (DTRACE_COND) {
      log_write("[DTRACE] READ  %s: addr=" FMT_PADDR ", offset=0x%08x, len=%d, data=" FMT_WORD "\n",
                map->name, addr, offset, len, ret);
    }
  );
  return ret;
}

/***************************************************************************************
 * map_write - 向 MMIO 地址写入数据
 * 
 * 【执行流程】
 * 1. 检查写入长度是否有效（1-8 字节）
 * 2. 检查地址是否在设备允许范围内（check_bound）
 * 3. 计算相对于设备基址的偏移
 * 4. 先将数据写入 io_space（修改设备寄存器的值）
 * 5. 调用设备的 IO 处理函数（如果有的话）
 *    - 这一步让设备对写操作产生响应
 *    - 例如写入控制寄存器可能触发设备动作
 * 
 * 【为什么 callback 在 host_write 之后调用？】
 * 设备通常需要先看到新的寄存器值，再做出反应。
 * 例如用户写入命令寄存器，设备需要先读到命令，再执行。
 * 这样的流程顺序最符合硬件的行为。
 * 
 * 【参数说明】
 * @param addr: 要写入的物理地址
 * @param len:  写入长度（1、2、4 或 8 字节）
 * @param data: 要写入的数据
 * @param map:  设备的 IOMap 结构
 ***************************************************************************************/
void map_write(paddr_t addr, int len, word_t data, IOMap *map) {
  assert(len >= 1 && len <= 8);
  check_bound(map, addr);
  paddr_t offset = addr - map->low;
  // Device trace: 记录设备写操作
  IFDEF(CONFIG_DTRACE,
    if (DTRACE_COND) {
      log_write("[DTRACE] WRITE %s: addr=" FMT_PADDR ", offset=0x%08x, len=%d, data=" FMT_WORD "\n",
                map->name, addr, offset, len, data);
    }
  );
  host_write(map->space + offset, len, data);
  invoke_callback(map->callback, offset, len, true);
}
