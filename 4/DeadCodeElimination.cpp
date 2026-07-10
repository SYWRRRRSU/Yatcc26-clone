#include "DeadCodeElimination.hpp"
#include <unordered_set>
#include <vector>

using namespace llvm;

PreservedAnalyses
DeadCodeElimination::run(Module& mod, ModuleAnalysisManager& mam)
{
  int eliminated = 0;
  bool changed = false;

  // 收集所有被 load 过的全局变量，未被读取的全局变量 store 可安全删除。
  std::unordered_set<GlobalVariable*> usedGVs;
  for (auto& func : mod) {
    for (auto& bb : func) {
      for (auto& inst : bb) {
        if (auto load{ dyn_cast<LoadInst>(&inst) }; load) {
          Value* ptr = load->getPointerOperand();
          if (auto gv = dyn_cast<GlobalVariable>(ptr)) {
            usedGVs.insert(gv);
          }
        }
      }
    }
  }

  mOut << "used_GVs:";
  for (auto* usedGV : usedGVs) {
    mOut << usedGV->getName();
  }
  mOut << "\n";

  // 遍历所有函数
  for (Function& func : mod) {
    // 遍历所有基本块
    for (BasicBlock& bb : func) {
      std::vector<Instruction*> instToErase;
      std::unordered_set<Instruction*> instToEraseSet;

      auto markForErase = [&](Instruction* inst) {
        if (instToEraseSet.insert(inst).second) {
          instToErase.push_back(inst);
          ++eliminated;
          changed = true;
        }
      };

      // 从后往前遍历基本块指令
      for (auto instIt = bb.rbegin(); instIt != bb.rend(); ++instIt) {
        Instruction& inst = *instIt;

        // 检查指令是否没有用户（包括显式和隐式用户）
        if (inst.isBinaryOp()) {
          // 检查二元运算符结果有无用户
          if (inst.use_empty()) {
            markForErase(&inst);
            continue;
          } else {
            bool allUsersDeleted = true;
            for (auto* user : inst.users()) {
              if (auto* uInst = dyn_cast<Instruction>(user);
                  !instToEraseSet.count(uInst)) {
                allUsersDeleted = false;
              }
            }

            if (allUsersDeleted) {
              markForErase(&inst);
              continue;
            }
          }

        } else if (auto* store = dyn_cast<StoreInst>(&inst)) {
          // 删除写入未被读取全局变量的 store。
          Value* ptr{ store->getPointerOperand() };
          if (auto* gv{ dyn_cast<GlobalVariable>(ptr) };
              gv && !usedGVs.count(gv)) {
            mOut << "remove global variable:\t" << gv->getName() << "\n";
            markForErase(&inst);
          }
        }
      }

      // 统一删除收集到的指令
      for (Instruction* inst : instToErase) {
        mOut << "erase instruction:\t" << inst->getName() << "\n";
        inst->eraseFromParent();
      }
    }
  }

  mOut << "DeadCodeElimination removed " << eliminated << " instructions\n";
  return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
