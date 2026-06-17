#include "ExtractLoopVariable.hpp"
#include <functional>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/Analysis/DominanceFrontier.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <vector>

using namespace llvm;

static void
doLICM(Loop* L, LoopInfo& LI)
{
  // 获取循环的preheader，不存在则退出
  BasicBlock* preheader = L->getLoopPreheader();
  if (!preheader) {
    return;
  }

  // 收集循环内所有非PHI非终结指令
  std::vector<Instruction*> worklist;
  for (BasicBlock* BB : L->blocks()) {
    for (Instruction& I : *BB) {
      if (isa<PHINode>(I) || I.isTerminator())
        continue;
      worklist.push_back(&I);
    }
  }

  // 记录已移出的指令
  SmallPtrSet<Instruction*, 16> moved;

  bool changed = true;
  while (changed) {
    changed = false;
    std::vector<Instruction*> nextWorklist; // 存储本轮无法移动的指令

    for (Instruction* I : worklist) {
      if (moved.count(I))
        continue; // 已移动则跳过

      // 检查操作数是否都是循环不变量
      bool canMove = true;
      for (Value* op : I->operands()) {
        Instruction* opInst = dyn_cast<Instruction>(op);
        if (!opInst)
          continue; // 常量或参数

        // 操作数在循环内且未移出，则当前指令不能移动
        if (L->contains(opInst) && !moved.count(opInst)) {
          canMove = false;
          break;
        }
      }

      // 特殊处理store指令：检查地址和值是否都是循环不变量
      if (auto* store = dyn_cast<StoreInst>(I)) {
        Value* addr = store->getPointerOperand();
        Value* value = store->getValueOperand();

        // 检查地址是否循环不变量
        if (auto* addrInst = dyn_cast<Instruction>(addr)) {
          if (L->contains(addrInst) && !moved.count(addrInst)) {
            canMove = false;
          }
        }

        // 检查值是否循环不变量
        if (auto* valueInst = dyn_cast<Instruction>(value)) {
          if (L->contains(valueInst) && !moved.count(valueInst)) {
            canMove = false;
          }
        }

        // Store指令可能有副作用，但如果是循环不变量可以外提
      }
      // 保守处理其他可能产生副作用的指令
      else if (canMove && (I->mayHaveSideEffects() || I->mayReadFromMemory())) {
        canMove = false;
      }

      if (!canMove) {
        nextWorklist.push_back(I); // 本轮无法移动，加入下一轮
        continue;
      }

      // 移动指令到preheader末尾
      Instruction* term = preheader->getTerminator();
      I->moveBefore(term);
      moved.insert(I); // 标记为已移动
      changed = true;  // 本轮有变动
    }
    worklist = std::move(nextWorklist); // 更新工作列表
  }
}

PreservedAnalyses
ExtractLoopVariable::run(Module& mod, ModuleAnalysisManager& mam)
{
  FunctionAnalysisManager fam;
  PassBuilder pb;
  fam.registerPass([&] { return LoopAnalysis(); });
  pb.registerFunctionAnalyses(fam);

  // 递归处理循环（深度优先，先内层后外层）
  std::function<void(Loop*, LoopInfo&)> processLoop =
    [&](Loop* L, LoopInfo& LI) {
      // 先处理子循环
      for (Loop* subLoop : L->getSubLoops())
        processLoop(subLoop, LI);
      // 再处理当前循环
      doLICM(L, LI);
    };

  for (Function& func : mod) {
    if (func.isDeclaration())
      continue;

    LoopInfo& LI = fam.getResult<LoopAnalysis>(func);
    for (Loop* LP : LI)
      processLoop(LP, LI); // 处理每个最外层循环
  }

  // 返回修改标记（因为我们修改了IR）
  return PreservedAnalyses::none();
}