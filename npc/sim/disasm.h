#ifndef __DISASM_H__
#define __DISASM_H__

#include <string>
#include <cstdint>

// 将 32 位机器码转换为汇编字符串
std::string disassemble(uint32_t inst);

#endif // __DISASM_H__
