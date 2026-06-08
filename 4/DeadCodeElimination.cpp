#include "DeadCodeElimination.hpp"
#include <algorithm>
#include <unordered_set>
#include <vector>

using namespace llvm;

PreservedAnalyses
DeadCodeElimination::run(Module& mod, ModuleAnalysisManager& mam)
{
  int eliminated = 0;

  // 收集所有被load过的全局变量
  std::unordered_set<GlobalVariable*> used_GVs;
  for (auto& func : mod) {
    for (auto& bb : func) {
      for (auto& inst : bb) {
        if (auto load{ dyn_cast<LoadInst>(&inst) }; load) {
          Value* ptr = load->getPointerOperand();
          if (auto gv = dyn_cast<GlobalVariable>(ptr)) {
            // 如果被load过就说明是有用的
            used_GVs.insert(gv);
          }
        }
      }
    }
  }

  mOut << "used_GVs:";
  for (auto* used_gv : used_GVs) {
    mOut << used_gv->getName();
  }
  mOut << "\n";

  // 遍历所有函数
  for (Function& func : mod) {
    // 遍历所有基本块
    for (BasicBlock& bb : func) {
      std::vector<Instruction*> instToErase;

      auto IsInInstToErase{ [&instToErase](Instruction* inst) {
        return std::find(instToErase.cbegin(), instToErase.cend(), inst) !=
               instToErase.cend();
      } };

      // 从后往前遍历基本块指令
      for (auto instIt = bb.rbegin(); instIt != bb.rend(); ++instIt) {
        Instruction& inst = *instIt;

        // 检查指令是否没有用户（包括显式和隐式用户）
        if (inst.isBinaryOp()) {
          // 检查二元运算符结果有无用户
          if (inst.use_empty()) {
            instToErase.push_back(&inst);
            ++eliminated;
            continue;
          } else {
            bool allUsersDeleted = true;
            for (auto* user : inst.users()) {
              if (auto* uInst = dyn_cast<Instruction>(user);
                  !IsInInstToErase(uInst)) {
                allUsersDeleted = false;
              }
            }

            if (allUsersDeleted) {
              instToErase.push_back(&inst);
              ++eliminated;
              continue;
            }
          }

          
        } else if (auto* store = dyn_cast<StoreInst>(&inst)) { // 检查有没有store指令向未被使用过的全局常量存储东西
          Value* ptr{ store->getPointerOperand() };
          // 如果这个全局变量没被用过的话就清除掉这条store指令
          if (auto* gv{ dyn_cast<GlobalVariable>(ptr) };
              gv && !used_GVs.count(gv)) {
            instToErase.push_back(&inst);
            mOut << "remove global variable:\t" << gv->getName() << "\n";
            ++eliminated;
          }
        }
      }

      // 统一删除收集到的指令
      for (Instruction* inst : instToErase) {
        mOut << "erase instruction:\t" << inst->getName() << "\n";
        inst->eraseFromParent();
        eliminated++;
      }
    }
  }

  return PreservedAnalyses::all();
}
