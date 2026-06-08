#include "CommonSubexpressionElimination.hpp"

using namespace llvm;

namespace std {
template<>
struct hash<std::tuple<Instruction::BinaryOps, Value*, Value*>>
{
  size_t operator()(
    const std::tuple<Instruction::BinaryOps, Value*, Value*>& key) const
  {
    auto& op = std::get<0>(key);
    auto& a = std::get<1>(key);
    auto& b = std::get<2>(key);

    size_t seed = 0;
    // Combine操作码
    seed ^= hash<Instruction::BinaryOps>()(op) + 0x9e3779b9 + (seed << 6) +
            (seed >> 2);
    // Combine操作数A
    seed ^= hash<Value*>()(a) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    // Combine操作数B
    seed ^= hash<Value*>()(b) + 0x9e3779b9 + (seed << 6) + (seed >> 2);

    return seed;
  }
};

template<>
struct equal_to<std::tuple<Instruction::BinaryOps, Value*, Value*>>
{
  bool operator()(
    const std::tuple<Instruction::BinaryOps, Value*, Value*>& lhs,
    const std::tuple<Instruction::BinaryOps, Value*, Value*>& rhs) const
  {
    return std::get<0>(lhs) == std::get<0>(rhs) &&
           std::get<1>(lhs) == std::get<1>(rhs) &&
           std::get<2>(lhs) == std::get<2>(rhs);
  }
};

}

PreservedAnalyses
CommonSubexpressionElimination::run(Module& mod, ModuleAnalysisManager& mam)
{
  int cseTimes = 0;

  for (auto& func : mod) {
    for (auto& bb : func) {
        
      // 使用哈希表存储已存在的表达式，键为（操作码，左操作数，右操作数）
      std::unordered_map<
        std::tuple<Instruction::BinaryOps, Value*, Value*>,
        Value*,
        std::hash<std::tuple<Instruction::BinaryOps, Value*, Value*>>,
        std::equal_to<std::tuple<Instruction::BinaryOps, Value*, Value*>>>
        exprMap;

      std::vector<Instruction*> instToErase;

      for (auto& inst : bb) {
        // 处理二元运算指令
        if (auto binOp = dyn_cast<BinaryOperator>(&inst)) {
          Value* lhs = binOp->getOperand(0);
          Value* rhs = binOp->getOperand(1);
          Instruction::BinaryOps op = binOp->getOpcode();

          // 生成表达式唯一标识
          auto key = std::make_tuple(op, lhs, rhs);

          // 检查是否已存在相同表达式
          if (exprMap.count(key)) {
            // 替换所有用户为已存在的计算结果
            binOp->replaceAllUsesWith(exprMap[key]);
            instToErase.push_back(binOp);
            ++cseTimes;
          } else {
            // 记录当前表达式的计算结果
            exprMap[key] = binOp;
          }
        }
      }

      // 删除所有被替换的指令
      for (auto& i : instToErase)
        i->eraseFromParent();
    }
  }

  mOut << "CSE optimization completed. Eliminated " << cseTimes
       << " common subexpressions.\n";
  return PreservedAnalyses::all();
}
