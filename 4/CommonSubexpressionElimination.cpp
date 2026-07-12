#include "CommonSubexpressionElimination.hpp"
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Operator.h>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace llvm;

namespace {

void
hashCombine(std::size_t& seed, std::size_t value)
{
  seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

struct ExpressionKey
{
  unsigned opcode = 0;
  Type* type = nullptr;
  CmpInst::Predicate predicate = CmpInst::BAD_ICMP_PREDICATE;
  bool inBounds = false;
  bool noUnsignedWrap = false;
  bool noSignedWrap = false;
  bool exact = false;
  Type* gepSourceType = nullptr;
  std::vector<Value*> operands;

  bool operator==(const ExpressionKey& other) const
  {
    return opcode == other.opcode && type == other.type &&
           predicate == other.predicate && inBounds == other.inBounds &&
           noUnsignedWrap == other.noUnsignedWrap &&
           noSignedWrap == other.noSignedWrap && exact == other.exact &&
           gepSourceType == other.gepSourceType && operands == other.operands;
  }
};

struct ExpressionKeyHash
{
  std::size_t operator()(const ExpressionKey& key) const
  {
    std::size_t seed = 0;
    hashCombine(seed, std::hash<unsigned>()(key.opcode));
    hashCombine(seed, std::hash<Type*>()(key.type));
    hashCombine(seed, std::hash<int>()(key.predicate));
    hashCombine(seed, std::hash<bool>()(key.inBounds));
    hashCombine(seed, std::hash<bool>()(key.noUnsignedWrap));
    hashCombine(seed, std::hash<bool>()(key.noSignedWrap));
    hashCombine(seed, std::hash<bool>()(key.exact));
    hashCombine(seed, std::hash<Type*>()(key.gepSourceType));
    for (Value* operand : key.operands) {
      hashCombine(seed, std::hash<Value*>()(operand));
    }
    return seed;
  }
};

bool
isSupportedBinaryOpcode(unsigned opcode)
{
  switch (opcode) {
    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::Mul:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
      return true;
    default:
      return false;
  }
}

void
addOperand(ExpressionKey& key, Value* value)
{
  key.operands.push_back(value);
}

void
addCommutativeOperands(ExpressionKey& key, Value* lhs, Value* rhs)
{
  if (std::less<Value*>()(rhs, lhs)) {
    key.operands.push_back(rhs);
    key.operands.push_back(lhs);
  } else {
    key.operands.push_back(lhs);
    key.operands.push_back(rhs);
  }
}

bool
buildExpressionKey(Instruction& inst, ExpressionKey& key)
{
  key.opcode = inst.getOpcode();
  key.type = inst.getType();

  if (auto* binOp = dyn_cast<BinaryOperator>(&inst)) {
    if (!isSupportedBinaryOpcode(binOp->getOpcode()))
      return false;

    if (auto* op = dyn_cast<OverflowingBinaryOperator>(binOp)) {
      key.noUnsignedWrap = op->hasNoUnsignedWrap();
      key.noSignedWrap = op->hasNoSignedWrap();
    }
    if (auto* op = dyn_cast<PossiblyExactOperator>(binOp)) {
      key.exact = op->isExact();
    }

    if (binOp->getOpcode() == Instruction::Add ||
        binOp->getOpcode() == Instruction::Mul) {
      addCommutativeOperands(key, binOp->getOperand(0), binOp->getOperand(1));
    } else {
      addOperand(key, binOp->getOperand(0));
      addOperand(key, binOp->getOperand(1));
    }
    return true;
  }

  if (auto* cmp = dyn_cast<ICmpInst>(&inst)) {
    key.predicate = cmp->getPredicate();
    addOperand(key, cmp->getOperand(0));
    addOperand(key, cmp->getOperand(1));
    return true;
  }

  if (auto* gep = dyn_cast<GetElementPtrInst>(&inst)) {
    key.inBounds = gep->isInBounds();
    key.gepSourceType = gep->getSourceElementType();
    for (Use& operand : gep->operands()) {
      addOperand(key, operand.get());
    }
    return true;
  }

  return false;
}

} // namespace

PreservedAnalyses
CommonSubexpressionElimination::run(Module& mod, ModuleAnalysisManager& mam)
{
  int cseTimes = 0;
  bool changed = false;

  for (auto& func : mod) {
    for (auto& bb : func) {
      std::unordered_map<ExpressionKey, Value*, ExpressionKeyHash> exprMap;

      std::vector<Instruction*> instToErase;

      for (auto& inst : bb) {
        if (inst.isTerminator() || isa<LoadInst>(&inst) || isa<StoreInst>(&inst) ||
            isa<CallBase>(&inst))
          continue;

        ExpressionKey key;
        if (!buildExpressionKey(inst, key))
          continue;

        auto [it, inserted] = exprMap.emplace(std::move(key), &inst);
        if (!inserted) {
          inst.replaceAllUsesWith(it->second);
          instToErase.push_back(&inst);
          ++cseTimes;
          changed = true;
        }
      }

      // 删除所有被替换的指令
      for (auto& i : instToErase)
        i->eraseFromParent();
    }
  }

  mOut << "CSE optimization completed. Eliminated " << cseTimes
       << " common subexpressions.\n";
  return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
