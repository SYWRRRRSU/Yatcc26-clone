#include "DeadCodeElimination.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace llvm;

namespace {

bool
canRemoveUnusedInstruction(Instruction& inst)
{
  return !inst.isTerminator() && !inst.mayHaveSideEffects() &&
         !inst.mayReadFromMemory();
}

} // namespace

PreservedAnalyses
DeadCodeElimination::run(Module& mod, ModuleAnalysisManager& mam)
{
  int eliminated = 0;
  bool changed = false;

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
      std::unordered_set<Instruction*> instToEraseSet;

      auto markForErase{ [&](Instruction* inst) {
        if (instToEraseSet.insert(inst).second) {
          instToErase.push_back(inst);
          ++eliminated;
          changed = true;
        }
      } };

      // 删除同一基本块内被后续 store 覆盖、且中间没有内存读写的 store。
      std::unordered_map<Value*, StoreInst*> lastStore;
      for (Instruction& inst : bb) {
        if (auto* store = dyn_cast<StoreInst>(&inst)) {
          if (store->isVolatile() || store->isAtomic()) {
            lastStore.clear();
            continue;
          }

          Value* ptr = store->getPointerOperand();
          if (auto it = lastStore.find(ptr); it != lastStore.end()) {
            markForErase(it->second);
          }

          lastStore.clear();
          lastStore[ptr] = store;
          continue;
        }

        if (inst.mayReadFromMemory() || inst.mayWriteToMemory()) {
          lastStore.clear();
        }
      }

      // 从后往前遍历基本块指令
      for (auto instIt = bb.rbegin(); instIt != bb.rend(); ++instIt) {
        Instruction& inst = *instIt;

        if (canRemoveUnusedInstruction(inst)) {
          if (inst.use_empty()) {
            markForErase(&inst);
            continue;
          }

          bool allUsersDeleted = true;
          for (auto* user : inst.users()) {
            auto* uInst = dyn_cast<Instruction>(user);
            if (!uInst || !instToEraseSet.count(uInst)) {
              allUsersDeleted = false;
              break;
            }
          }

          if (allUsersDeleted) {
            markForErase(&inst);
            continue;
          }

        } else if (auto* store = dyn_cast<StoreInst>(&inst)) { // 检查有没有store指令向未被使用过的全局常量存储东西
          Value* ptr{ store->getPointerOperand() };
          // 如果这个全局变量没被用过的话就清除掉这条store指令
          if (auto* gv{ dyn_cast<GlobalVariable>(ptr) };
              gv && !used_GVs.count(gv) && !store->isVolatile() &&
              !store->isAtomic()) {
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
