#include <iostream>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/raw_ostream.h>

#include "CommonSubexpressionElimination.hpp"
#include "ConstantFolding.hpp"
#include "ConstantPropagation.hpp"
#include "Mem2Reg.hpp"
#include "StaticCallCounter.hpp"
#include "StaticCallCounterPrinter.hpp"
#include "StrengthReduction.hpp"
#include "DeadCodeElimination.hpp"
#include "InstructionCombining.hpp"
#include "IfCombine.hpp"
#include "ExtractLoopVariable.hpp"
#include "FunctionInlining.hpp"

#ifdef TASK4_LLM

namespace {

void
addSafePasses(llvm::ModulePassManager& mpm)
{
  // task4-llm 的安全回退路径：复用 task4-classic 已验证的 Pass 序列，
  // 避免模型/API 异常或非法 pass sequence 影响输出 IR 正确性。
  mpm.addPass(StaticCallCounterPrinter(llvm::errs()));
  mpm.addPass(Mem2Reg());
  mpm.addPass(ConstantPropagation(llvm::errs()));
  mpm.addPass(ConstantFolding(llvm::errs()));
  mpm.addPass(CommonSubexpressionElimination(llvm::errs()));
  mpm.addPass(DeadCodeElimination(llvm::errs()));
  mpm.addPass(InstructionCombining(llvm::errs()));
  mpm.addPass(IfCombine(llvm::errs()));
  mpm.addPass(ExtractLoopVariable(llvm::errs()));
  mpm.addPass(FunctionInlining(llvm::errs()));
  mpm.addPass(ConstantPropagation(llvm::errs()));
  mpm.addPass(ConstantFolding(llvm::errs()));
  mpm.addPass(CommonSubexpressionElimination(llvm::errs()));
  mpm.addPass(DeadCodeElimination(llvm::errs()));
  mpm.addPass(InstructionCombining(llvm::errs()));
  mpm.addPass(IfCombine(llvm::errs()));
  mpm.addPass(ExtractLoopVariable(llvm::errs()));
  mpm.addPass(StrengthReduction(llvm::errs()));
}

} // namespace

#endif

void
opt(llvm::Module& mod)
{
  using namespace llvm;

  // 定义分析pass的管理器
  LoopAnalysisManager lam;
  FunctionAnalysisManager fam;
  CGSCCAnalysisManager cgam;
  ModuleAnalysisManager mam;
  ModulePassManager mpm;

  // 注册分析pass的管理器
  PassBuilder pb;
  pb.registerModuleAnalyses(mam);
  pb.registerCGSCCAnalyses(cgam);
  pb.registerFunctionAnalyses(fam);
  pb.registerLoopAnalyses(lam);
  pb.crossRegisterProxies(lam, fam, cgam, mam);

  // 添加分析pass到管理器中
  mam.registerPass([]() { return StaticCallCounter(); });

#ifdef TASK4_LLM

  addSafePasses(mpm);

#else

  // 传统 LLVM Pass 来进行编译优化
  // 添加优化pass到管理器中
  mpm.addPass(StaticCallCounterPrinter(llvm::errs()));
  mpm.addPass(Mem2Reg());
  mpm.addPass(ConstantPropagation(llvm::errs()));
  mpm.addPass(ConstantFolding(llvm::errs()));
  mpm.addPass(CommonSubexpressionElimination(llvm::errs()));
  mpm.addPass(DeadCodeElimination(llvm::errs()));
  mpm.addPass(InstructionCombining(llvm::errs()));
  mpm.addPass(IfCombine(llvm::errs()));
  mpm.addPass(ExtractLoopVariable(llvm::errs()));
  mpm.addPass(FunctionInlining(llvm::errs()));
  mpm.addPass(ConstantPropagation(llvm::errs()));
  mpm.addPass(ConstantFolding(llvm::errs()));
  mpm.addPass(CommonSubexpressionElimination(llvm::errs()));
  mpm.addPass(DeadCodeElimination(llvm::errs()));
  mpm.addPass(InstructionCombining(llvm::errs()));
  mpm.addPass(IfCombine(llvm::errs()));
  mpm.addPass(ExtractLoopVariable(llvm::errs()));
  mpm.addPass(StrengthReduction(llvm::errs()));


#endif

  // 运行优化pass
  mpm.run(mod, mam);
}

int
main(int argc, char** argv)
{
  if (argc != 3) {
    std::cout << "Usage: " << argv[0] << " <input> <output>\n";
    return -1;
  }

  llvm::LLVMContext ctx;

  llvm::SMDiagnostic err;
  auto mod = llvm::parseIRFile(argv[1], err, ctx);
  if (!mod) {
    std::cout << "Error: unable to parse input file: " << argv[1] << '\n';
    err.print(argv[0], llvm::errs());
    return -2;
  }

  std::error_code ec;
  llvm::StringRef outPath(argv[2]);
  llvm::raw_fd_ostream outFile(outPath, ec);
  if (ec) {
    std::cout << "Error: unable to open output file: " << argv[2] << '\n';
    return -3;
  }

  opt(*mod); // IR的优化发生在这里

  mod->print(outFile, nullptr, false, true);
  if (llvm::verifyModule(*mod, &llvm::outs()))
    return 3;
}
