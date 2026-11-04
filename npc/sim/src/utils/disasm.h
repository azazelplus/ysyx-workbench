#ifndef __DISASM_H__
#define __DISASM_H__

#include <string>
#include <cstdint>

// 初始化反汇编器（可选，用于 LLVM/Capstone 后端）
void init_disasm(const char* triple);

// 将 32 位机器码转换为汇编字符串
std::string disassemble(uint32_t inst);

#endif // __DISASM_H__
