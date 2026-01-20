/***************************************************************************************
 * iringbuf.c - 指令环形缓冲区 (Instruction Ring Buffer)
 * 
 * 功能：记录最近执行的若干条指令，在程序出错时输出，方便调试。
 * 
 * 原理：维护一个固定大小的环形缓冲区，每执行一条指令就记录到缓冲区中。
 *      当缓冲区满了，新的指令会覆盖最旧的指令。
 *      程序出错时，打印缓冲区中的所有指令，并用 "-->" 标记出错的指令。
 ***************************************************************************************/

#include <common.h>

#ifdef CONFIG_IRINGBUF

// 环形缓冲区大小（指令条数）
#define IRINGBUF_SIZE CONFIG_IRINGBUF_SIZE

// 单条指令的日志缓冲区大小
#define INST_LOG_SIZE 128

// 环形缓冲区结构
typedef struct {
  char logbuf[INST_LOG_SIZE];  // 指令日志（包含 PC、机器码、反汇编）
  bool valid;                   // 该条目是否有效
} IRingBufEntry;

// 环形缓冲区
static IRingBufEntry iringbuf[IRINGBUF_SIZE];

// 当前写入位置（下一条指令将写入的位置）
static int iringbuf_head = 0;

// 最后一条指令的位置（用于标记出错指令）
static int iringbuf_curr = -1;

/**
 * iringbuf_write - 将一条指令记录到环形缓冲区
 * @logbuf: 指令的日志字符串（来自 Decode 结构体的 logbuf）
 */
void iringbuf_write(const char *logbuf) {
  // 复制日志到当前位置
  strncpy(iringbuf[iringbuf_head].logbuf, logbuf, INST_LOG_SIZE - 1);
  iringbuf[iringbuf_head].logbuf[INST_LOG_SIZE - 1] = '\0';
  iringbuf[iringbuf_head].valid = true;
  
  // 记录当前位置
  iringbuf_curr = iringbuf_head;
  
  // 移动到下一个位置（环形）
  iringbuf_head = (iringbuf_head + 1) % IRINGBUF_SIZE;
}

/**
 * iringbuf_display - 打印环形缓冲区的内容
 * 
 * 从最旧的指令开始打印，用 "-->" 标记最后执行的指令（通常是出错的指令）。
 */
void iringbuf_display() {
  if (iringbuf_curr < 0) {
    Log("iringbuf: No instructions recorded.");
    return;
  }
  
  Log("========== Instruction Ring Buffer ==========");
  
  // 从 head 开始遍历（head 指向最旧的位置，因为它是下一个要被覆盖的位置）
  // 但如果缓冲区还没满，需要从第一个有效条目开始
  int start = iringbuf_head;
  int count = 0;
  
  for (int i = 0; i < IRINGBUF_SIZE; i++) {
    int idx = (start + i) % IRINGBUF_SIZE;
    if (iringbuf[idx].valid) {
      // 用 "-->" 标记最后执行的指令
      const char *marker = (idx == iringbuf_curr) ? " --> " : "     ";
      printf("%s%s\n", marker, iringbuf[idx].logbuf);
      count++;
    }
  }
  
  Log("========== End of Ring Buffer (%d instructions) ==========", count);
}

#endif /* CONFIG_IRINGBUF */
