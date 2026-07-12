#include "IfCombine.hpp" // 假设这是包含 IfCombine 类声明的头文件

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h" // For PreservedAnalyses
#include <vector>

using namespace llvm;

PreservedAnalyses
IfCombine::run(Module& mod, ModuleAnalysisManager& mam)
{
  bool changed = false;

  for (Function& F : mod) {
    if (F.isDeclaration()) { // 跳过外部函数声明
      continue;
    }

    std::vector<Instruction*> instToErase;
    // 遍历函数中的所有基本块。
    for (BasicBlock& BB : F) {

      Instruction* Terminator = BB.getTerminator();
      if (!Terminator) { // 基本块可能没有终结指令（例如，在构建过程中）
        continue;
      }

      // 检查终结指令是否为条件跳转指令 (BranchInst)
      if (BranchInst* BI = dyn_cast<BranchInst>(Terminator)) {
        if (BI->isConditional()) {
          // 获取条件值
          Value* Cond = BI->getCondition();

          // 检查条件是否为常量整数
          if (ConstantInt* CI = dyn_cast<ConstantInt>(Cond)) {
            // 检查常量条件是否为 'true' 
            if (CI->isOne()) {
              BasicBlock* ThenBB = BI->getSuccessor(0); // 'if.then' 分支

              // 创建一个新的无条件跳转指令，跳转到 'ThenBB'
              BranchInst::Create(ThenBB, BI);
              instToErase.push_back(BI);
              changed = true;
            } else if (CI->isZero()) { // 如果是 false 就跳转到 else 分支
              BasicBlock* ElseBB = BI->getSuccessor(1); // 'if.else' 分支

              BranchInst::Create(ElseBB, BI);
              instToErase.push_back(BI);
              changed = true;
            }
          }
        }
      }
    }
    for (auto* inst : instToErase) {
      inst->eraseFromParent();
    }
  }

  return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}