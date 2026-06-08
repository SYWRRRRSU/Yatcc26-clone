#pragma once

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Support/raw_ostream.h>

// 公共子表达式消除
class CommonSubexpressionElimination : public llvm::PassInfoMixin<CommonSubexpressionElimination>
{
public:
  explicit CommonSubexpressionElimination(llvm::raw_ostream& out)
    : mOut(out)
  {
  }

  llvm::PreservedAnalyses run(llvm::Module& mod,
                              llvm::ModuleAnalysisManager& mam);

private:
  llvm::raw_ostream& mOut;
};
