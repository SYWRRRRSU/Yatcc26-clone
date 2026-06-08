#include "FunctionInlining.hpp"
#include <llvm/ADT/SCCIterator.h>
#include <llvm/Analysis/CallGraph.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Transforms/Utils/Cloning.h>

using namespace llvm;

PreservedAnalyses
FunctionInlining::run(Module& mod, ModuleAnalysisManager& mam)
{
  // 获取函数调用图
  CallGraph& CG = mam.getResult<CallGraphAnalysis>(mod);

  // 步骤1: 识别所有递归函数
  std::unordered_set<Function*> recursiveFuncs;
  for (auto sccIt = scc_begin(&CG); !sccIt.isAtEnd(); ++sccIt) {
    const std::vector<CallGraphNode*>& SCC = *sccIt;

    if (SCC.size() > 1) { // 间接递归
      for (CallGraphNode* CGN : SCC) {
        if (Function* F = CGN->getFunction()) {
          recursiveFuncs.insert(F);
        }
      }
    } else if (SCC.size() == 1) { // 检查自递归
      CallGraphNode* Node = SCC[0];
      for (auto& callRecord : *Node) {
        if (callRecord.second == Node) {
          if (Function* F = Node->getFunction()) {
            recursiveFuncs.insert(F);
          }
          break;
        }
      }
    }
  }

  // 步骤2: 收集所有可内联的调用点（忽略noinline属性）
  std::vector<CallBase*> callsToInline;
  for (Function& F : mod) {
    if (F.isDeclaration() || recursiveFuncs.count(&F))
      continue;

    for (BasicBlock& BB : F) {
      for (Instruction& I : BB) {
        CallBase* CB = dyn_cast<CallBase>(&I);
        if (!CB)
          continue;

        Function* callee = CB->getCalledFunction();
        // 修改：移除NoInline检查 && 允许特定的小函数内联
        if (!callee || callee->isDeclaration() || callee->isVarArg() ||
            recursiveFuncs.count(callee)) {
          continue;
        }

        // 额外条件：只内联小型函数（根据基本块数量判断）
        if (callee->size() <= 1) { // 允许单个基本块的函数
          callsToInline.push_back(CB);
        }
      }
    }
  }

  // 步骤3: 执行内联操作
  InlineFunctionInfo IFI;
  bool anyInlined = false;
  for (CallBase* CB : callsToInline) {
    if (InlineFunction(*CB, IFI).isSuccess()) {
      anyInlined = true;
    }
  }

  // 步骤4: 删除不再使用的函数
  std::vector<Function*> toRemove;
  for (Function& F : mod) {
    if (F.isDeclaration() || F.getName() == "main" || F.hasAddressTaken() ||
        !F.hasInternalLinkage()) {
      continue;
    }

    if (F.use_empty()) {
      toRemove.push_back(&F);
    }
  }

  for (Function* F : toRemove) {
    F->eraseFromParent();
  }

  return anyInlined || !toRemove.empty() ? PreservedAnalyses::none()
                                         : PreservedAnalyses::all();
}