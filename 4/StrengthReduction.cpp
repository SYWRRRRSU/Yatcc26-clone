#include "StrengthReduction.hpp"
#include <llvm/Support/MathExtras.h>

using namespace llvm;

namespace {
bool
isPowerOf2(uint64_t x)
{
  return x != 0 && (x & (x - 1)) == 0;
}

bool
isIntegerType(Value* val)
{
  return val->getType()->isIntegerTy();
}
}

PreservedAnalyses
StrengthReduction::run(Module& mod, ModuleAnalysisManager& mam)
{
  int strengthReductionTimes = 0;
  bool changed = false;

  for (auto& func : mod) {
    for (auto& bb : func) {
      std::vector<Instruction*> instToErase;
      for (auto& inst : bb) {
        if (auto binOp = dyn_cast<BinaryOperator>(&inst)) {
          Value* lhs = binOp->getOperand(0);
          Value* rhs = binOp->getOperand(1);
          auto constRhs = dyn_cast<ConstantInt>(rhs);
          auto constLhs = dyn_cast<ConstantInt>(lhs);

          // 如果没有常数或者常数是负数都跳过
          if (!constRhs && !constLhs) {
            continue;
          } else if (constLhs && constLhs->getSExtValue() < 0) {
            continue;
          } else if (constRhs && constRhs->getSExtValue() < 0) {
            continue;
          }


          // 对乘法、无符号除法、无符号取模做强度削减
          switch (binOp->getOpcode()) {
            case Instruction::Mul: {
              if (constRhs) {
                if (!isIntegerType(lhs))
                  break;
                uint64_t value = constRhs->getSExtValue();
                if (isPowerOf2(value)) {
                  unsigned shift = Log2_64(value);
                  auto* newInst = BinaryOperator::CreateShl(
                    lhs, ConstantInt::get(binOp->getType(), shift), "", binOp);
                  binOp->replaceAllUsesWith(newInst);
                  instToErase.push_back(binOp);
                  ++strengthReductionTimes;
                  changed = true;
                }
              } else if (constLhs) {
                if (!isIntegerType(rhs))
                  break;
                uint64_t value = constLhs->getSExtValue();
                if (isPowerOf2(value)) {
                  unsigned shift = Log2_64(value);
                  auto* newInst = BinaryOperator::CreateShl(
                    rhs, ConstantInt::get(binOp->getType(), shift), "", binOp);
                  binOp->replaceAllUsesWith(newInst);
                  instToErase.push_back(binOp);
                  ++strengthReductionTimes;
                  changed = true;
                }
              }
              break;
            }
            case Instruction::UDiv: {
              if (constRhs) {
                if (!isIntegerType(lhs))
                  break;
                uint64_t value = constRhs->getSExtValue();
                if (isPowerOf2(value)) {
                  unsigned shift = Log2_64(value);
                  auto* newInst = BinaryOperator::CreateLShr(
                    lhs, ConstantInt::get(binOp->getType(), shift), "", binOp);
                  binOp->replaceAllUsesWith(newInst);
                  instToErase.push_back(binOp);
                  ++strengthReductionTimes;
                  changed = true;
                }
              }
              break;
            }
            case Instruction::URem: {
              if (constRhs) {
                if (!isIntegerType(lhs))
                  break;
                uint64_t value = constRhs->getSExtValue();
                if (isPowerOf2(value)) {
                  auto* mask = ConstantInt::get(binOp->getType(), value - 1);
                  auto* newInst =
                    BinaryOperator::CreateAnd(lhs, mask, "", binOp);
                  binOp->replaceAllUsesWith(newInst);
                  instToErase.push_back(binOp);
                  ++strengthReductionTimes;
                  changed = true;
                }
              }
              break;
            }
            default:
              break;
          }
        }
      }

      for (auto& i : instToErase)
        i->eraseFromParent();
    }
  }

  mOut << "StrengthReduction running...\nOptimized " << strengthReductionTimes
       << " instructions\n";
  return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
