#pragma once

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Support/raw_ostream.h>

// 内联非递归的小函数调用，并删除不再使用的内部函数。
class FunctionInlining : public llvm::PassInfoMixin<FunctionInlining>
{
public:
  explicit FunctionInlining(llvm::raw_ostream& out)
    : mOut(out)
  {
  }

  llvm::PreservedAnalyses run(llvm::Module& mod,
                              llvm::ModuleAnalysisManager& mam);

private:
  llvm::raw_ostream& mOut;
};
