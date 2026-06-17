#include "FunctionInlining.hpp"

using namespace llvm;

PreservedAnalyses
FunctionInlining::run(Module& mod, ModuleAnalysisManager& mam)
{
  // 安全提交版本中该 Pass 不做任何 IR 修改。
  return PreservedAnalyses::all();
}