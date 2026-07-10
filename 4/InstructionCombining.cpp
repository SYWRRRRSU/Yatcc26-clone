#include "InstructionCombining.hpp"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include <unordered_set>
#include <vector>

using namespace llvm;

namespace {

ConstantInt*
getConstInt(Value* value)
{
  return dyn_cast<ConstantInt>(value);
}

void
markForErase(Instruction* inst,
             std::vector<Instruction*>& instToErase,
             std::unordered_set<Instruction*>& toEraseSet)
{
  if (toEraseSet.insert(inst).second)
    instToErase.push_back(inst);
}

Value*
createAddWithConstant(Value* base, const APInt& offset, Instruction* insertBefore)
{
  if (!base->getType()->isIntegerTy())
    return nullptr;

  auto* type = cast<IntegerType>(base->getType());
  if (offset.isZero())
    return base;

  if (offset.isNegative()) {
    return BinaryOperator::CreateSub(
      base, ConstantInt::get(type, -offset), "", insertBefore);
  }

  return BinaryOperator::CreateAdd(
    base, ConstantInt::get(type, offset), "", insertBefore);
}

bool
asBasePlusConstant(BinaryOperator* binOp, Value*& base, APInt& offset)
{
  ConstantInt* const0 = getConstInt(binOp->getOperand(0));
  ConstantInt* const1 = getConstInt(binOp->getOperand(1));

  switch (binOp->getOpcode()) {
    case Instruction::Add:
      if (const0) {
        base = binOp->getOperand(1);
        offset = const0->getValue();
        return base->getType()->isIntegerTy();
      }
      if (const1) {
        base = binOp->getOperand(0);
        offset = const1->getValue();
        return base->getType()->isIntegerTy();
      }
      return false;
    case Instruction::Sub:
      if (const1) {
        base = binOp->getOperand(0);
        offset = -const1->getValue();
        return base->getType()->isIntegerTy();
      }
      return false;
    default:
      return false;
  }
}

bool
tryCombineAddSub(BinaryOperator* binOp,
                 std::vector<Instruction*>& instToErase,
                 std::unordered_set<Instruction*>& toEraseSet)
{
  Value* lhs = binOp->getOperand(0);
  Value* rhs = binOp->getOperand(1);
  auto* lhsInst = dyn_cast<BinaryOperator>(lhs);
  auto* rhsInst = dyn_cast<BinaryOperator>(rhs);
  auto* lhsConst = getConstInt(lhs);
  auto* rhsConst = getConstInt(rhs);

  BinaryOperator* nested = nullptr;
  Value* base = nullptr;
  APInt offset(1, 0);

  if (lhsInst && lhsInst->hasOneUse() && rhsConst &&
      asBasePlusConstant(lhsInst, base, offset)) {
    nested = lhsInst;
    if (binOp->getOpcode() == Instruction::Sub)
      offset -= rhsConst->getValue();
    else
      offset += rhsConst->getValue();
  } else if (binOp->getOpcode() == Instruction::Add && rhsInst &&
             rhsInst->hasOneUse() && lhsConst &&
             asBasePlusConstant(rhsInst, base, offset)) {
    nested = rhsInst;
    offset += lhsConst->getValue();
  } else {
    return false;
  }

  if (toEraseSet.count(nested))
    return false;

  Value* replacement = createAddWithConstant(base, offset, binOp);
  if (!replacement)
    return false;

  binOp->replaceAllUsesWith(replacement);
  markForErase(binOp, instToErase, toEraseSet);
  markForErase(nested, instToErase, toEraseSet);
  return true;
}

bool
tryCombineMul(BinaryOperator* binOp,
              std::vector<Instruction*>& instToErase,
              std::unordered_set<Instruction*>& toEraseSet)
{
  if (binOp->getOpcode() != Instruction::Mul)
    return false;

  BinaryOperator* nested = nullptr;
  ConstantInt* outerConst = nullptr;
  if (auto* lhsInst = dyn_cast<BinaryOperator>(binOp->getOperand(0));
      lhsInst && lhsInst->hasOneUse() && lhsInst->getOpcode() == Instruction::Mul) {
    nested = lhsInst;
    outerConst = getConstInt(binOp->getOperand(1));
  } else if (auto* rhsInst = dyn_cast<BinaryOperator>(binOp->getOperand(1));
             rhsInst && rhsInst->hasOneUse() &&
             rhsInst->getOpcode() == Instruction::Mul) {
    nested = rhsInst;
    outerConst = getConstInt(binOp->getOperand(0));
  }

  if (!nested || !outerConst || toEraseSet.count(nested))
    return false;

  Value* base = nullptr;
  ConstantInt* innerConst = nullptr;
  if ((innerConst = getConstInt(nested->getOperand(0)))) {
    base = nested->getOperand(1);
  } else if ((innerConst = getConstInt(nested->getOperand(1)))) {
    base = nested->getOperand(0);
  } else {
    return false;
  }

  if (!base->getType()->isIntegerTy())
    return false;

  APInt merged = innerConst->getValue() * outerConst->getValue();
  auto* newMul = BinaryOperator::CreateMul(
    base,
    ConstantInt::get(cast<IntegerType>(base->getType()), merged),
    "",
    binOp);
  binOp->replaceAllUsesWith(newMul);
  markForErase(binOp, instToErase, toEraseSet);
  markForErase(nested, instToErase, toEraseSet);
  return true;
}

bool
tryFoldConstantCast(CastInst* castInst,
                    std::vector<Instruction*>& instToErase,
                    std::unordered_set<Instruction*>& toEraseSet)
{
  auto* constant = dyn_cast<Constant>(castInst->getOperand(0));
  if (!constant)
    return false;

  Constant* folded =
    ConstantExpr::getCast(castInst->getOpcode(), constant, castInst->getType());
  castInst->replaceAllUsesWith(folded);
  markForErase(castInst, instToErase, toEraseSet);
  return true;
}

bool
trySimplifyZExtBoolICmp(ICmpInst* cmp,
                        std::vector<Instruction*>& instToErase,
                        std::unordered_set<Instruction*>& toEraseSet)
{
  auto predicate = cmp->getPredicate();
  if (predicate != ICmpInst::ICMP_EQ && predicate != ICmpInst::ICMP_NE)
    return false;

  auto* zext = dyn_cast<ZExtInst>(cmp->getOperand(0));
  auto* constant = dyn_cast<ConstantInt>(cmp->getOperand(1));
  if (!zext || !constant) {
    zext = dyn_cast<ZExtInst>(cmp->getOperand(1));
    constant = dyn_cast<ConstantInt>(cmp->getOperand(0));
  }

  if (!zext || !constant || !zext->getOperand(0)->getType()->isIntegerTy(1))
    return false;

  Value* bit = zext->getOperand(0);
  bool isEq = predicate == ICmpInst::ICMP_EQ;
  Value* replacement = nullptr;

  if (constant->isZero()) {
    replacement = isEq
                    ? static_cast<Value*>(new ICmpInst(
                        cmp, ICmpInst::ICMP_EQ, bit,
                        ConstantInt::getFalse(cmp->getContext())))
                    : bit;
  } else if (constant->isOne()) {
    replacement = isEq
                    ? bit
                    : static_cast<Value*>(new ICmpInst(
                        cmp, ICmpInst::ICMP_EQ, bit,
                        ConstantInt::getFalse(cmp->getContext())));
  } else {
    replacement =
      ConstantInt::get(cmp->getContext(), APInt(1, isEq ? 0 : 1));
  }

  cmp->replaceAllUsesWith(replacement);
  markForErase(cmp, instToErase, toEraseSet);
  return true;
}

} // namespace

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

        if (auto* castInst = dyn_cast<CastInst>(&inst)) {
          if (tryFoldConstantCast(castInst, instToErase, toEraseSet)) {
            ++combined;
            changed = true;
            continue;
          }
        }

        if (auto* cmp = dyn_cast<ICmpInst>(&inst)) {
          if (trySimplifyZExtBoolICmp(cmp, instToErase, toEraseSet)) {
            ++combined;
            changed = true;
            continue;
          }
        }

        if (auto* select = dyn_cast<SelectInst>(&inst)) {
          if (auto* cond = dyn_cast<ConstantInt>(select->getCondition())) {
            select->replaceAllUsesWith(
              cond->isOne() ? select->getTrueValue() : select->getFalseValue());
            markForErase(select, instToErase, toEraseSet);
            ++combined;
            changed = true;
            continue;
          }
        }

        // 检查是否为二元加法指令且只有一个使用者
        if (auto* binOp = dyn_cast<BinaryOperator>(&inst)) {
          if (binOp->getOpcode() == Instruction::Add ||
              binOp->getOpcode() == Instruction::Sub) {
            if (tryCombineAddSub(binOp, instToErase, toEraseSet)) {
              ++combined;
              changed = true;
              continue;
            }
          }

          if (tryCombineMul(binOp, instToErase, toEraseSet)) {
            ++combined;
            changed = true;
            continue;
          }

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
            markForErase(userInst, instToErase, toEraseSet);
            markForErase(binOp, instToErase, toEraseSet);
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