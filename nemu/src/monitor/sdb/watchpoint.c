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
***************************************************************************************/

#include "sdb.h"
#include "debug.h"  // 为了使用Assert宏

// Watchpoint pool size. if you need more, change it.
#define NR_WP 32

typedef struct watchpoint {
  // 监视点序号
  int NO;
  // 链表下一个.
  struct watchpoint *next;

  /* TODO: Add more members if necessary */
  // 监视的表达式字符串
  char expr[256];
  // 表达式的上一次值（用于检测变化）
  word_t old_value;

} WP;
// WP是watchpoint的别名. 
// (说法存疑???)这种写法称为不透明结构体, 允许C像C++一样实现封装. 其他.c文件可以通过包含sdb.h来使用WP类型, 但无法直接访问watchpoint结构体的成员. 另一种(更丑)的常见写法就是普通结构体: 你要把整个typedef struct watchpoint { ... } WP;放在sdb.h中. 然后watchpoint.c只需要包含sdb.h. 其他.c谁需要用这个结构体就包含sdb.h. 这些.c单元都可以看到结构体所有成员. 不优美.


// 声明监视点池.
static WP wp_pool[NR_WP] = {};
// head指向已使用的监视点链表头. free_指向空闲监视点链表头.
static WP *head = NULL, *free_ = NULL;

// 初始化监视点池: 
// 1.给所有节点.NO编号; 
// 2.把所有节点.next指向下一个节点;
// 3.head置NULL, free_指向wp_pool头.
void init_wp_pool() {
  int i;
  for (i = 0; i < NR_WP; i ++) {
    wp_pool[i].NO = i;
    wp_pool[i].next = (i == NR_WP - 1 ? NULL : &wp_pool[i + 1]);
  }

  head = NULL;
  free_ = wp_pool;
}


// 从链表中返回一个空闲的监视点结构
WP* new_wp() {
  // 如果没有空闲监视点了
  if (free_ == NULL) {
    Assert(0, "No free watchpoint available! Max watchpoint number is %d", NR_WP);
    return NULL;
  }
  
  // 从 free_ 链表头取出一个监视点
  WP *wp = free_;
  free_ = free_->next;
  
  // 将监视点插入到 head 链表头部
  wp->next = head;
  head = wp;
  
  return wp;
}

// 将 wp 归还到 free_ 链表
void free_wp(WP *wp) {
  // 确保 wp 不为空
  Assert(wp != NULL, "Cannot free NULL watchpoint");
  
  // 从 head 链表中移除 wp
  if (head == wp) {
    // 如果 wp 是头节点
    head = head->next;
  } else {
    // 在链表中查找 wp 的前一个节点
    WP *prev = head;
    while (prev != NULL && prev->next != wp) {
      prev = prev->next;
    }
    
    // 如果找到了，从链表中删除
    if (prev != NULL) {
      prev->next = wp->next;
    } else {
      Assert(0, "Watchpoint not found in active list");
    }
  }
  
  // 将 wp 插入到 free_ 链表头部
  wp->next = free_;
  free_ = wp;
}

// 根据编号查找监视点
WP* find_wp(int no) {
  WP *wp = head;
  while (wp != NULL) {
    if (wp->NO == no) {
      return wp;
    }
    wp = wp->next;
  }
  return NULL;
}

// 显示所有监视点
void display_wp() {
  if (head == NULL) {
    printf("No watchpoints.\n");
    return;
  }
  
  printf("Num     Expression              Value\n");
  WP *wp = head;
  while (wp != NULL) {
    printf("%-8d%-24s0x%08x\n", wp->NO, wp->expr, wp->old_value);
    wp = wp->next;
  }
}

// 创建监视点：设置表达式和初始值. 包装new_wp().
// 返回监视点编号，失败返回 -1
int create_wp(const char *expr_str, word_t value) {
  WP *wp = new_wp();
  if (wp == NULL) {
    return -1;
  }
  
  // 保存表达式和初始值
  strncpy(wp->expr, expr_str, sizeof(wp->expr) - 1);
  wp->expr[sizeof(wp->expr) - 1] = '\0';
  wp->old_value = value;
  
  return wp->NO;
}

// 删除监视点. 包装free_wp().
// 返回 0 成功，-1 失败
int delete_wp(int no) {
  WP *wp = find_wp(no);
  if (wp == NULL) {
    return -1;
  }
  
  free_wp(wp);
  return 0;
}

// 扫描所有监视点，检查表达式的值是否发生变化
// 返回值：true 表示有监视点被触发，false 表示没有
bool scan_watchpoints() {
  WP *wp = head;
  bool triggered = false;
  
  while (wp != NULL) {
    // 对监视点的表达式求值
    bool success = false;
    word_t new_value = expr(wp->expr, &success);
    
    if (!success) {
      printf("Warning: failed to evaluate watchpoint %d expression '%s'\n", 
             wp->NO, wp->expr);
      wp = wp->next;
      continue;
    }
    
    // 比较新值和旧值
    if (new_value != wp->old_value) {
      printf("\nWatchpoint %d: %s\n", wp->NO, wp->expr);
      printf("Old value = 0x%08x\n", wp->old_value);
      printf("New value = 0x%08x\n", new_value);
      
      // 更新旧值
      wp->old_value = new_value;
      triggered = true;
    }
    
    wp = wp->next;
  }
  
  return triggered;
}

