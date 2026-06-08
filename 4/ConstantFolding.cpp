#include "ConstantFolding.hpp"

using namespace llvm;

PreservedAnalyses
ConstantFolding::run(Module& mod, ModuleAnalysisManager& mam)
{
  int constFoldTimes = 0;

  // 遍历所有函数
  for (auto& func : mod) {
    // 遍历每个函数的基本块
    for (auto& bb : func) {
      std::vector<Instruction*> instToErase;
      // 遍历每个基本块的指令
      for (auto& inst : bb) {
        // 判断当前指令是否是二元运算指令
        if (auto binOp = dyn_cast<BinaryOperator>(&inst)) {
          // 获取二元运算指令的左右操作数，并尝试转换为常整数
          Value* lhs = binOp->getOperand(0);
          Value* rhs = binOp->getOperand(1);
          auto constLhs = dyn_cast<ConstantInt>(lhs);
          auto constRhs = dyn_cast<ConstantInt>(rhs);
          switch (binOp->getOpcode()) {
            case Instruction::Add: {
              // 若左右操作数均为整数常量，则进行常量折叠与use替换
              if (constLhs && constRhs) {
                binOp->replaceAllUsesWith(ConstantInt::getSigned(
                  binOp->getType(),
                  constLhs->getSExtValue() + constRhs->getSExtValue()));
                instToErase.push_back(binOp);
                ++constFoldTimes;
              } else if (constRhs && constRhs->getSExtValue() == 0) {
                // 右操作数为0时，直接用左操作数替换指令
                binOp->replaceAllUsesWith(binOp->getOperand(0));
                instToErase.push_back(binOp);
                ++constFoldTimes;
              } else if (constLhs && constLhs->getSExtValue() == 0) {
                // 左操作数为0时，直接用右操作数替换指令
                binOp->replaceAllUsesWith(binOp->getOperand(1));
                instToErase.push_back(binOp);
                ++constFoldTimes;
              }
              break;
            }
            case Instruction::Sub: {
              if (constLhs && constRhs) {
                binOp->replaceAllUsesWith(ConstantInt::getSigned(
                  binOp->getType(),
                  constLhs->getSExtValue() - constRhs->getSExtValue()));
                instToErase.push_back(binOp);
                ++constFoldTimes;
              } else if (constRhs && constRhs->getSExtValue() == 0) {
                // 右操作数为0时，直接用左操作数替换指令
                binOp->replaceAllUsesWith(binOp->getOperand(0));
                instToErase.push_back(binOp);
                ++constFoldTimes;
              }
              break;
            }
            case Instruction::Mul: {
              if (constLhs && constRhs) {
                binOp->replaceAllUsesWith(ConstantInt::getSigned(
                  binOp->getType(),
                  constLhs->getSExtValue() * constRhs->getSExtValue()));
                instToErase.push_back(binOp);
                ++constFoldTimes;
              } else if (constRhs && constRhs->getSExtValue() == 1) {
                // 右操作数为1时，直接用左操作数替换指令
                binOp->replaceAllUsesWith(binOp->getOperand(0));
                instToErase.push_back(binOp);
                ++constFoldTimes;
              } else if (constLhs && constLhs->getSExtValue() == 1) {
                // 左操作数为1时，直接用右操作数替换指令
                binOp->replaceAllUsesWith(binOp->getOperand(1));
                instToErase.push_back(binOp);
                ++constFoldTimes;
              } else if (constRhs && constRhs->getSExtValue() == 0) {
                // 右操作数为0时，直接用0替换指令
                binOp->replaceAllUsesWith(
                  ConstantInt::getSigned(binOp->getType(), 0));
                instToErase.push_back(binOp);
                ++constFoldTimes;
              } else if (constLhs && constLhs->getSExtValue() == 0) {
                // 左操作数为0时，直接用0替换指令
                binOp->replaceAllUsesWith(
                  ConstantInt::getSigned(binOp->getType(), 0));
                instToErase.push_back(binOp);
                ++constFoldTimes;
              }
              break;
            }
            case Instruction::UDiv:
            case Instruction::SDiv: {
              if (constLhs && constRhs) {
                binOp->replaceAllUsesWith(ConstantInt::getSigned(
                  binOp->getType(),
                  constLhs->getSExtValue() / constRhs->getSExtValue()));
                instToErase.push_back(binOp);
                ++constFoldTimes;
              } else if (constRhs && constRhs->getSExtValue() == 1) {
                // 右操作数为1时，直接用左操作数替换指令
                binOp->replaceAllUsesWith(binOp->getOperand(0));
                instToErase.push_back(binOp);
                ++constFoldTimes;
              } else if (constLhs && constLhs->getSExtValue() == 0) {
                // 左操作数为0时，直接用0替换指令
                binOp->replaceAllUsesWith(
                  ConstantInt::getSigned(binOp->getType(), 0));
                instToErase.push_back(binOp);
                ++constFoldTimes;
              }
              break;
            }
            case Instruction::SRem: {
              if (constRhs && constRhs->getSExtValue() == 1) {
                // 右操作数为1时，直接用左操作数替换指令
                binOp->replaceAllUsesWith(
                  ConstantInt::getSigned(binOp->getType(), 0));
                instToErase.push_back(binOp);
                ++constFoldTimes;
              }
              break;
            }
            default:
              break;
          }
        } else if (auto comOp = dyn_cast<ICmpInst>(&inst)) {
          Value* lhs = comOp->getOperand(0);
          Value* rhs = comOp->getOperand(1);
          auto constLhs = dyn_cast<ConstantInt>(lhs);
          auto constRhs = dyn_cast<ConstantInt>(rhs);

          if (constLhs && constRhs) {
            APInt L = constLhs->getValue();
            APInt R = constRhs->getValue();
            bool result = false;
            bool handled = true; // 添加处理状态标志

            switch (comOp->getPredicate()) {
              // 有符号比较
              case ICmpInst::ICMP_SGT:
                result = L.sgt(R);
                break;
              case ICmpInst::ICMP_SLT:
                result = L.slt(R);
                break;
              case ICmpInst::ICMP_SGE:
                result = L.sge(R);
                break;
              case ICmpInst::ICMP_SLE:
                result = L.sle(R);
                break;

              // 无符号比较
              case ICmpInst::ICMP_UGT:
                result = L.ugt(R);
                break;
              case ICmpInst::ICMP_ULT:
                result = L.ult(R);
                break;
              case ICmpInst::ICMP_UGE:
                result = L.uge(R);
                break;
              case ICmpInst::ICMP_ULE:
                result = L.ule(R);
                break;

              // 相等比较
              case ICmpInst::ICMP_EQ:
                result = (L == R);
                break;
              case ICmpInst::ICMP_NE:
                result = (L != R);
                break;

              default:
                handled = false; // 标记未处理类型
                break;
            }

            if (handled) { // 仅处理支持的谓词
              comOp->replaceAllUsesWith(
                ConstantInt::get(inst.getContext(), APInt(1, result)));
              instToErase.push_back(&inst);
              ++constFoldTimes;
            }
          }
        }
      }

        // 统一删除被折叠为常量的指令
        for (auto& i : instToErase)
          i->eraseFromParent();
      }
    }
  

  mOut << "ConstantFolding running...\nTo eliminate " << constFoldTimes
       << " instructions\n";
  return PreservedAnalyses::all();
}
