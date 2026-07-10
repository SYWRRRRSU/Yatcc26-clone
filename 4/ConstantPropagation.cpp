#include "ConstantPropagation.hpp"
#include <unordered_map>
#include <vector>

using namespace llvm;

// 仅处理全局非数组常量的常量传播
PreservedAnalyses
ConstantPropagation::run(Module& mod, ModuleAnalysisManager& mam)
{
  int constFoldTimes = 0;
  bool changed = false;
  std::unordered_map<Value*, Constant*> globalConstants;

  // 1. 预处理：收集所有被Store修改的非数组全局变量
  std::unordered_map<GlobalVariable*, bool> modifiedGVs;
  for (auto& func : mod) {
    for (auto& bb : func) {
      for (auto& inst : bb) {
        if (auto store = dyn_cast<StoreInst>(&inst)) {
          Value* ptr = store->getPointerOperand();
          if (auto gv = dyn_cast<GlobalVariable>(ptr)) {
            // 检查目标全局变量是否是数组类型
            if (auto init = gv->getInitializer()) {
              if (init->getType()->isArrayTy()) {
                continue; // 跳过数组类型的全局变量
              }
            }
            modifiedGVs[gv] = true;
          }
        }
      }
    }
  }

  // 2. 收集符合条件的全局变量（未修改且非数组）
  for (auto& gvar : mod.global_values()) {
    auto gv = dyn_cast<GlobalVariable>(&gvar);
    if (!gv)
      continue;

    if (auto init = gv->getInitializer()) {
      // 检查初始值是否为数组类型
      if (init->getType()->isArrayTy()) {
        continue; // 跳过数组类型的全局变量
      }

      if (!(modifiedGVs.count(gv) && modifiedGVs[gv])) {
        globalConstants[gv] = init;
      }
    }
  }

  // 遍历所有函数
  for (auto& func : mod) {
    // 遍历每个基本块
    for (auto& bb : func) {
      std::vector<Instruction*> instToErase;
      std::unordered_map<Value*, Constant*> localConstants;

      // 单基本块内精确指针常量传播。遇到任意可能写内存的未知指令时清空，
      // 避免跨 alias/call 做不安全替换。
      for (auto& inst : bb) {
        if (auto* load = dyn_cast<LoadInst>(&inst)) {
          if (load->isVolatile() || load->isAtomic()) {
            localConstants.clear();
            continue;
          }

          Value* ptr = load->getPointerOperand();
          if (auto it = localConstants.find(ptr); it != localConstants.end()) {
            load->replaceAllUsesWith(it->second);
            instToErase.push_back(load);
            ++constFoldTimes;
            changed = true;
          } else if (auto it = globalConstants.find(ptr);
                     it != globalConstants.end()) {
            load->replaceAllUsesWith(it->second);
            instToErase.push_back(load);
            ++constFoldTimes;
            changed = true;
          }
          continue;
        }

        if (auto* store = dyn_cast<StoreInst>(&inst)) {
          localConstants.clear();

          if (store->isVolatile() || store->isAtomic())
            continue;

          if (auto* constant = dyn_cast<Constant>(store->getValueOperand())) {
            localConstants[store->getPointerOperand()] = constant;
          }
          continue;
        }

        if (inst.mayWriteToMemory()) {
          localConstants.clear();
        }
      }

      // 批量删除被优化的指令
      for (auto& i : instToErase)
        i->eraseFromParent();
    }
  }

  mOut << "ConstantPropagation running...\nOptimized " << constFoldTimes
       << " instructions\n";
  return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
