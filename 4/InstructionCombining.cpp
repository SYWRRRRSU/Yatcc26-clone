#include "InstructionCombining.hpp"
#include "llvm/IR/Instructions.h"
#include <unordered_set>
#include <vector>

using namespace llvm;

PreservedAnalyses
InstructionCombining::run(Module& mod, ModuleAnalysisManager& mam)
{
  int combined = 0;
  bool changed = false;
  std::unordered_set<Instruction*> toEraseSet;

  for (auto& func : mod) {
    for (auto& bb : func) {
      std::vector<Instruction*> instToErase;

      for (auto& inst : bb) {
        // 跳过已标记删除的指令
        if (toEraseSet.find(&inst) != toEraseSet.end())
          continue;

        // 检查是否为二元加法指令且只有一个使用者
        if (auto* binOp = dyn_cast<BinaryOperator>(&inst)) {
          if (binOp->getOpcode() == Instruction::Add && binOp->hasOneUse()) {
            Value* op0 = binOp->getOperand(0);
            Value* op1 = binOp->getOperand(1);

            // 检查操作数中是否包含常数
            ConstantInt* const0 = dyn_cast<ConstantInt>(op0);
            ConstantInt* const1 = dyn_cast<ConstantInt>(op1);
            if (!const0 && !const1)
              continue;

            // 获取唯一使用者指令
            Instruction* userInst = dyn_cast<Instruction>(*binOp->user_begin());
            if (!userInst || userInst->getParent() != &bb ||
                !isa<BinaryOperator>(userInst) ||
                userInst->getOpcode() != Instruction::Add) {
              continue;
            }

            // 检查使用者指令的操作数
            Value* userOp0 = userInst->getOperand(0);
            Value* userOp1 = userInst->getOperand(1);
            ConstantInt* userConst = nullptr;

            // 确定使用者指令中的常数操作数
            if (userOp0 == binOp) {
              userConst = dyn_cast<ConstantInt>(userOp1);
            } else if (userOp1 == binOp) {
              userConst = dyn_cast<ConstantInt>(userOp0);
            }
            if (!userConst)
              continue;

            // 合并常数
            Constant* mergedConst = nullptr;
            Value* baseVar = nullptr;

            // 确定基础变量和合并的常数值
            if (const0) {
              baseVar = op1;
              mergedConst = ConstantInt::get(
                const0->getType(), const0->getValue() + userConst->getValue());
            } else {
              baseVar = op0;
              mergedConst = ConstantInt::get(
                const1->getType(), const1->getValue() + userConst->getValue());
            }

            // 创建新的加法指令
            BinaryOperator* newAdd =
              BinaryOperator::CreateAdd(baseVar, mergedConst, "", userInst);
            // 替换使用者指令的所有使用
            userInst->replaceAllUsesWith(newAdd);

            // 标记旧指令待删除
            instToErase.push_back(userInst);
            instToErase.push_back(binOp);
            toEraseSet.insert(userInst);
            toEraseSet.insert(binOp);
            combined++;
            changed = true;
          }
        }
      }

      for (auto* i : instToErase) {
        i->eraseFromParent();
      }
    }
  }

  mOut << "InstructionCombining merged " << combined << " instructions\n";
  return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}