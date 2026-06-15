#include "DeadCodeElimination.hpp"
#include <llvm/IR/CFG.h>
#include <llvm/IR/InstrTypes.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace llvm;

namespace {

bool
canRemoveUnusedInstruction(Instruction& inst)
{
  return !isa<CallBase>(&inst) && !inst.isTerminator() &&
         !inst.mayHaveSideEffects() && !inst.mayReadFromMemory();
}

bool
removeUnreachableBlocks(Function& func, int& eliminated)
{
  if (func.empty())
    return false;

  std::unordered_set<BasicBlock*> reachable;
  std::vector<BasicBlock*> worklist{ &func.getEntryBlock() };

  while (!worklist.empty()) {
    BasicBlock* bb = worklist.back();
    worklist.pop_back();
    if (!reachable.insert(bb).second)
      continue;

    for (BasicBlock* succ : successors(bb)) {
      worklist.push_back(succ);
    }
  }

  std::vector<BasicBlock*> deadBlocks;
  for (BasicBlock& bb : func) {
    if (!reachable.count(&bb))
      deadBlocks.push_back(&bb);
  }

  for (BasicBlock* bb : deadBlocks) {
    for (BasicBlock* succ : successors(bb)) {
      succ->removePredecessor(bb);
    }
  }

  for (BasicBlock* bb : deadBlocks) {
    bb->eraseFromParent();
    ++eliminated;
  }

  return !deadBlocks.empty();
}

} // namespace

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
    if (!func.isDeclaration())
      changed |= removeUnreachableBlocks(func, eliminated);

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

      // 删除同一基本块内被后续 store 覆盖、且中间没有任何内存读写的 store。
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
      bool localChanged = true;
      while (localChanged) {
        localChanged = false;
        for (auto instIt = bb.rbegin(); instIt != bb.rend(); ++instIt) {
          Instruction& inst = *instIt;
          if (instToEraseSet.count(&inst))
            continue;

          if (canRemoveUnusedInstruction(inst)) {
            if (inst.use_empty()) {
              markForErase(&inst);
              localChanged = true;
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
              localChanged = true;
              continue;
            }

          } else if (auto* store = dyn_cast<StoreInst>(&inst)) {
            // 删除写入未被读取全局变量的 store。
            Value* ptr{ store->getPointerOperand() };
            if (auto* gv{ dyn_cast<GlobalVariable>(ptr) };
                gv && !usedGVs.count(gv) && !store->isVolatile() &&
                !store->isAtomic()) {
              mOut << "remove global variable:\t" << gv->getName() << "\n";
              markForErase(&inst);
              localChanged = true;
            }
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
