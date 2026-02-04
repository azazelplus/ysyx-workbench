/***************************************************************************************
 * disasm.cpp - 基于 LLVM 的 RISC-V 反汇编器
 * 
 * 使用 LLVM MC (Machine Code) 库提供准确、完整的反汇编功能
 * 参考：NEMU/src/utils/disasm.cc
 ***************************************************************************************/

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInstPrinter.h"
#if LLVM_VERSION_MAJOR >= 14
#include "llvm/MC/TargetRegistry.h"
#if LLVM_VERSION_MAJOR >= 15
#include "llvm/MC/MCSubtargetInfo.h"
#endif
#else
#include "llvm/Support/TargetRegistry.h"
#endif
#include "llvm/Support/TargetSelect.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#if LLVM_VERSION_MAJOR < 11
#error Please use LLVM with major version >= 11
#endif

#include "utils/disasm.h"
#include <cstdio>
#include <cstring>
#include <cassert>

using namespace llvm;

// LLVM 全局对象
static llvm::MCDisassembler *gDisassembler = nullptr;
static llvm::MCSubtargetInfo *gSTI = nullptr;
static llvm::MCInstPrinter *gIP = nullptr;

/**
 * init_disasm - 初始化 LLVM 反汇编器
 * @triple: 目标三元组，如 "riscv32" 或 "riscv64"
 */
void init_disasm(const char *triple) {
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllDisassemblers();

    std::string errstr;
    std::string gTriple(triple);

    llvm::MCInstrInfo *gMII = nullptr;
    llvm::MCRegisterInfo *gMRI = nullptr;
    auto target = llvm::TargetRegistry::lookupTarget(gTriple, errstr);
    if (!target) {
        llvm::errs() << "Can't find target for " << gTriple << ": " << errstr << "\n";
        assert(0);
    }

    MCTargetOptions MCOptions;
    gSTI = target->createMCSubtargetInfo(gTriple, "", "");
    std::string isa = target->getName();
    if (isa == "riscv32" || isa == "riscv64") {
        gSTI->ApplyFeatureFlag("+m");
        gSTI->ApplyFeatureFlag("+a");
        gSTI->ApplyFeatureFlag("+c");
        gSTI->ApplyFeatureFlag("+f");
        gSTI->ApplyFeatureFlag("+d");
    }
    gMII = target->createMCInstrInfo();
    gMRI = target->createMCRegInfo(gTriple);
    auto AsmInfo = target->createMCAsmInfo(*gMRI, gTriple, MCOptions);
#if LLVM_VERSION_MAJOR >= 13
    auto llvmTripleTwine = Twine(triple);
    auto llvmtriple = llvm::Triple(llvmTripleTwine);
    auto Ctx = new llvm::MCContext(llvmtriple, AsmInfo, gMRI, nullptr);
#else
    auto Ctx = new llvm::MCContext(AsmInfo, gMRI, nullptr);
#endif
    gDisassembler = target->createMCDisassembler(*gSTI, *Ctx);
    gIP = target->createMCInstPrinter(llvm::Triple(gTriple),
        AsmInfo->getAssemblerDialect(), *AsmInfo, *gMII, *gMRI);
    gIP->setPrintImmHex(true);
    gIP->setPrintBranchImmAsAddress(true);
    if (isa == "riscv32" || isa == "riscv64")
        gIP->applyTargetSpecificCLOption("no-aliases");
    
    printf("[INFO] Disassembler initialized for: %s (using LLVM)\n", triple);
}

/**
 * disassemble - 将机器码反汇编为汇编字符串
 * @inst: 32位指令机器码
 * @return: 汇编指令字符串
 */
std::string disassemble(uint32_t inst) {
    if (inst == 0) return "nop";
    if (!gDisassembler) return "disasm-not-init";
    
    MCInst mcinst;
    uint8_t code[4] = {
        (uint8_t)(inst & 0xFF),
        (uint8_t)((inst >> 8) & 0xFF),
        (uint8_t)((inst >> 16) & 0xFF),
        (uint8_t)((inst >> 24) & 0xFF)
    };
    llvm::ArrayRef<uint8_t> arr(code, 4);
    uint64_t dummy_size = 0;
    gDisassembler->getInstruction(mcinst, dummy_size, arr, 0, llvm::nulls());

    std::string s;
    raw_string_ostream os(s);
    gIP->printInst(&mcinst, 0, "", *gSTI, os);

    // 跳过前导 tab
    size_t skip = s.find_first_not_of('\t');
    if (skip == std::string::npos) return s;
    return s.substr(skip);
}
