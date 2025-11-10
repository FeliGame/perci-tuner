// @Author: Qihaijun & Guocheng - NUDT

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "FP128ToDD.h"
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <set>

#define CF1(I, s) ChangeInstToCall(I, s, {I->getOperand(0)}, I->getType())
#define CF2(I, s)                                                              \
  ChangeInstToCall(I, s, {I->getOperand(0), I->getOperand(1)}, I->getType())
#define CF3(I, s)                                                              \
  ChangeInstToCall(I, s,                                                       \
                   {I->getOperand(0), I->getOperand(1), I->getOperand(2)},     \
                   I->getType())
#define CB2(I, s)                                                              \
  ChangeInstToCall(I, s, {I->getOperand(0), I->getOperand(1)},                 \
                   Type::getInt1Ty(M.getContext()))
#define CT1(I, s) ChangeInstToCall(I, s, {I->getOperand(0)}, I->getType())

//#include <unordered_map>
//
//std::unordered_map<std::string, void(*)(CallInst *, std::string, std::vector<Value *>, Type *)> funlist 

std::vector<AllocaInst*> Allocas;

template <typename T>
inline void FP128ToDD::ChangeInstToCall(T *I,
                                            const std::string &FunctionName,
                                            const std::vector<Value *> &Args,
                                            Type *ReturnType) {
  Instruction *Inst = dyn_cast<Instruction>(I);
#ifdef DEBUG
  // errs() << "Instruction : " << *Inst << "\n";
#endif
  Module *M = Inst->getModule();
  IRBuilder<> Builder(I);

  std::vector<Value *> newArgs;
  if (!(dyn_cast<StoreInst>(I))) {
    for (unsigned i = 0; i < Args.size(); i++) {
      auto *arg = Inst->getOperand(i);
#ifdef DEBUG
      // errs() << "Old operand " << i << " : " << *arg << "\n";
#endif
        newArgs.push_back(arg);
    }
  } else {
    newArgs = Args;
  }

  std::vector<Type *> aType;
  for (auto *arg : Args) {
    aType.push_back(arg->getType());
  }
  FunctionType *FuncTy = FunctionType::get(ReturnType, aType, false);
  Function *Callee = M->getFunction(FunctionName);
  if (!Callee || Callee->getFunctionType() != FuncTy) {
    Callee =
        Function::Create(FuncTy, Function::ExternalLinkage, FunctionName, M);
  }
  // Function *Callee = getOrCreateFunction(M, FunctionName, Args, ReturnType);
#ifdef DEBUG
  // errs() << "New Function : " << *Callee << "\n";
#endif
  // CallInst *Call = Builder.CreateCall(Callee, Args);
  CallInst *Call = Builder.CreateCall(Callee, newArgs);
  Call->setDebugLoc(I->getDebugLoc());

  I->replaceAllUsesWith(Call);
  erase.push_back(I);
}

inline std::string getBOName(BinaryOperator *bo) {
  switch (bo->getOpcode()) {
  case Instruction::FAdd:
    return "fp128_add";
  case Instruction::FSub:
    return "fp128_sub";
  case Instruction::FMul:
    return "fp128_mul";
  case Instruction::FDiv:
    return "fp128_div";
  default:
    ////errs() << "orther Binary Operator " << *bo << ".\n";
    return "";
  }
}

inline StringRef dropPrefixSuffix(StringRef name) {
	//errs() << "name " << name << "\n";
	name.consume_front("__isoc99_");
	name.consume_front("___");
	name.consume_front("__");

	name.consume_back("_chk");
	name.consume_back("_s");

	return name;
}

bool isScanfFamily(StringRef name) {
	name = dropPrefixSuffix(name);
	return  name == "scanf" || name == "fscanf" || name == "vscanf" ||
		name == "wscanf" || name == "fwscanf" || name == "vwscanf";
}

//Type *getUltimateElementType(Value *V) {
//	while (true) {
//		errs() << "In getUltimateElementType : " << *V << "\n";
//		Type *Ty = V->getType();
//		if (!Ty->isPointerTy()) return Ty;
//
//		if (auto *AI = dyn_cast<AllocaInst>(V))
//			return AI->getAllocatedType();
//
//		if (auto *GEP = dyn_cast<GetElementPtrInst>(V)) {
//			V = GEP->getOperand(1);
//			continue;
//		}
//
//		if (auto *LI = dyn_cast<LoadInst>(V)) {
//			V = LI->getPointerOperand();
//			continue;
//		}
//
//		bool progress = false;
//		for (User *U : V->users()) {
//			if (isa<GetElementPtrInst>(U) || isa<LoadInst>(U)) {
//				V = U;
//				progress = true;
//				break;
//			}
//		}
//		if (progress) continue;
//
//		errs() << "End\n";
//		break;
//		//llvm_unreachable("give up");
//	}
//}

GlobalVariable *getUnderlyingGlobalString(Value *V) {
	for (;;) {
		if (auto *GV = dyn_cast<GlobalVariable>(V))
			return GV->isConstant() ? GV : nullptr;

		if (auto *CE = dyn_cast<ConstantExpr>(V)) {
			if (CE->isCast() || CE->getOpcode() == Instruction::GetElementPtr) {
				V = CE->getOperand(0);
				continue;
			}
		}
		return nullptr;
	}
}

bool FP128ToDD::runOnModule(Module &M) {
  // 获取源操作数本身的lambda表达式（返回Value*）
  auto getDoubleSrcOperand = [](Value* operand) -> Value* {
      // 处理CastInst：转换指令的源操作数是其第一个操作数（被转换的值）
      if (auto* castInst = dyn_cast<CastInst>(operand)) {
          if (castInst->getSrcTy()->isDoubleTy()) {
              operand = castInst->getOperand(0);
          }
      }

      // 处理CallInst：转换函数的源操作数是其第一个参数
      else if (auto* callInst = dyn_cast<CallInst>(operand)) {
          auto* calledFunc = callInst->getCalledFunction();
          if (calledFunc && calledFunc->getName().str()=="FP64ToDD") {
              operand = callInst->getArgOperand(0);
          }
      }

      return operand;
  };
  for (auto &F : M) {
    for (auto &BB : F) {
      for (auto &I : BB) {
	if (auto *BinOp = dyn_cast<BinaryOperator>(&I)) { // 处理计算指令
          std::string funName = getBOName(BinOp);
          if (BinOp->getType()->isFP128Ty() &&
              !funName.empty()) { //处理fp128的add,sub,mul,div
            string suffix="";
            Value* originalOps[2];
            for (unsigned i = 0; i < BinOp->getNumOperands(); ++i) {
              auto *originalOp = getDoubleSrcOperand(BinOp->getOperand(i));
              originalOps[i] = originalOp;
              if (originalOp->getType()->isFP128Ty()) {
                suffix += "2";
              } else if (originalOp->getType()->isDoubleTy()) {
                suffix += "1";
              }
            }
            // 两个源操作数都是double，则不需要转换，跳过
            if (suffix == "11") break;
            // 否则用转换前源操作数替换当前操作数
            if (suffix == "12") {
              auto* castFromDoubleTy = dyn_cast<Instruction>(BinOp->getOperand(0));
              castFromDoubleTy->replaceAllUsesWith(originalOps[0]);
              castFromDoubleTy->eraseFromParent();
            } else if (suffix == "21") {
              auto* castFromDoubleTy = dyn_cast<Instruction>(BinOp->getOperand(1));
              castFromDoubleTy->replaceAllUsesWith(originalOps[1]);
              castFromDoubleTy->eraseFromParent();
            }
            funName += suffix;
            CF2(BinOp, funName);
          }
        } else if (I.getOpcode() == Instruction::FNeg) {
          Instruction *II = &I;
          if (II->getType()->isFP128Ty()) {
            CF1(II, "fp128_neg");
          }
        } else if (auto *FCmp = dyn_cast<FCmpInst>(&I)) { // 处理比较指令
          if (FCmp->getOperand(0)->getType()->isFP128Ty()) {
            switch (FCmp->getPredicate()) {
            case FCmpInst::FCMP_OGT:
            case FCmpInst::FCMP_UGT:
              CB2(FCmp, "fp128_gt");
              break;
            case FCmpInst::FCMP_OGE:
            case FCmpInst::FCMP_UGE:
              CB2(FCmp, "fp128_ge");
              break;
            case FCmpInst::FCMP_OLT:
            case FCmpInst::FCMP_ULT:
              CB2(FCmp, "fp128_lt");
              break;
            case FCmpInst::FCMP_OLE:
            case FCmpInst::FCMP_ULE:
              CB2(FCmp, "fp128_le");
              break;
            case FCmpInst::FCMP_OEQ:
            case FCmpInst::FCMP_UEQ:
              CB2(FCmp, "fp128_eq");
              break;
            case FCmpInst::FCMP_ONE:
            case FCmpInst::FCMP_UNE:
              CB2(FCmp, "fp128_ne");
              break;
            default:
              continue;
            }
          }
        } else if (auto *FCall = dyn_cast<CallInst>(&I)) { //处理函数调用
          //errs() << "Call I : " << *FCall << "\n";
          auto *Callee = FCall->getCalledFunction();
          if (Callee) {
	    if (Callee->getReturnType()->isFP128Ty()) {
              ////errs() << "FP128 Call : " << I << "\n";
              if (Callee->getName() == "llvm.fmuladd.f128") {
#ifdef DEBUG
                // errs() << "This call (" << I << ") need to deal." << "\n";
#endif
                std::string funName = "fp128_muladd";
                for (unsigned i = 0; i < FCall->getNumOperands(); ++i) {
                  if (FCall->getOperand(i)->getType()->isFP128Ty()) {
                    funName += "2";
                  } else if (FCall->getOperand(i)->getType()->isDoubleTy()) {
                    funName += "1";
                  }
                }
                CF3(FCall, funName);
              } else if (Callee->getName() == "acosl") {
                CF1(FCall, "fp128_acos");
              } else if (Callee->getName() == "asinl") {
                CF1(FCall, "fp128_asin");
              } else if (Callee->getName() == "atanl") {
                CF1(FCall, "fp128_atan");
              } else if (Callee->getName() == "atan2l") {
                CF2(FCall, "fp128_atan2");
              } else if (Callee->getName() == "cosl") {
                CF1(FCall, "fp128_cos");
              } else if (Callee->getName() == "sinl") {
                CF1(FCall, "fp128_sin");
              } else if (Callee->getName() == "tanl") {
                CF1(FCall, "fp128_tan");
              } else if (Callee->getName() == "coshl") {
                CF1(FCall, "fp128_cosh");
              } else if (Callee->getName() == "sinhl") {
                CF1(FCall, "fp128_sinh");
              } else if (Callee->getName() == "tanhl") {
                CF1(FCall, "fp128_tanh");
              } else if (Callee->getName() == "acoshl") {
                CF1(FCall, "fp128_acosh");
              } else if (Callee->getName() == "asinhl") {
                CF1(FCall, "fp128_asinh");
              } else if (Callee->getName() == "atanhl") {
                CF1(FCall, "fp128_atanh");
              } else if (Callee->getName() == "expl") {
                CF1(FCall, "fp128_exp");
              } else if (Callee->getName() == "frexpl") {
                CF2(FCall, "fp128_frexp");
              } else if (Callee->getName() == "ldexpl") {
                CF2(FCall, "fp128_ldexp");
              } else if (Callee->getName() == "logl") {
                CF1(FCall, "fp128_log");
              } else if (Callee->getName() == "log10l") {
                CF1(FCall, "fp128_log10");
              } else if (Callee->getName() == "modfl") {
                CF2(FCall, "fp128_modf");
              } else if (Callee->getName() == "expm1l") {
                CF1(FCall, "fp128_expm1");
              } else if (Callee->getName() == "log1pl") {
                CF1(FCall, "fp128_log1p");
              } else if (Callee->getName() == "logbl") {
                CF1(FCall, "fp128_logb");
              } else if (Callee->getName() == "exp2l") {
                CF1(FCall, "fp128_exp2");
              } else if (Callee->getName() == "log2l") {
                CF1(FCall, "fp128_log2");
              } else if (Callee->getName() == "powl") {
                CF2(FCall, "fp128_pow");
              } else if (Callee->getName() == "sqrtl") {
                CF1(FCall, "fp128_sqrt");
              } else if (Callee->getName() == "hypotl") {
                CF2(FCall, "fp128_hypot");
              } else if (Callee->getName() == "cbrtl") {
                CF1(FCall, "fp128_cbrt");
              } else if (Callee->getName() == "llvm.ceil.f128") {
                CF1(FCall, "fp128_ceil");
              } else if (Callee->getName() == "llvm.fabs.f128") {
                CF1(FCall, "fp128_fabs");
              } else if (Callee->getName() == "llvm.floor.f128") {
                CF1(FCall, "fp128_floor");
              } else if (Callee->getName() == "fmodl") {
                CF2(FCall, "fp128_fmod");
              } else if (Callee->getName() == "dreml") {
                CF2(FCall, "fp128_drem");
              } else if (Callee->getName() == "significandl") {
                CF1(FCall, "fp128_significand");
              } else if (Callee->getName() == "llvm.copysign.f128") {
                CF2(FCall, "fp128_copysign");
              } else if (Callee->getName() == "nanl") {
                CF1(FCall, "fp128_nan");
              } else if (Callee->getName() == "j0l") {
                CF1(FCall, "fp128_j0");
              } else if (Callee->getName() == "j1l") {
                CF1(FCall, "fp128_j1");
              } else if (Callee->getName() == "jnl") {
                CF2(FCall, "fp128_jn");
              } else if (Callee->getName() == "y0l") {
                CF1(FCall, "fp128_y0");
              } else if (Callee->getName() == "y1l") {
                CF1(FCall, "fp128_y1");
              } else if (Callee->getName() == "ynl") {
                CF2(FCall, "fp128_yn");
              } else if (Callee->getName() == "erfl") {
                CF1(FCall, "fp128_erf");
              } else if (Callee->getName() == "erfcl") {
                CF1(FCall, "fp128_erfc");
              } else if (Callee->getName() == "lgammal") {
                CF1(FCall, "fp128_lgamma");
              } else if (Callee->getName() == "tgammal") {
                CF1(FCall, "fp128_tgamma");
              } else if (Callee->getName() == "gammal") {
                CF1(FCall, "fp128_gamma");
              } else if (Callee->getName() == "lgammal_r") {
                CF2(FCall, "fp128_lgamma_r");
              } else if (Callee->getName() == "llvm.rint.f128") {
                CF1(FCall, "fp128_rint");
              } else if (Callee->getName() == "nextafterl") {
                CF2(FCall, "fp128_nextafter");
              } else if (Callee->getName() == "nexttowardl") {
                CF2(FCall, "fp128_nexttoward");
              } else if (Callee->getName() == "remainderl") {
                CF2(FCall, "fp128_remainder");
              } else if (Callee->getName() == "scalbnl") {
                CF2(FCall, "fp128_scalbn");
              } else if (Callee->getName() == "scalblnl") {
                CF2(FCall, "fp128_scalbln");
              } else if (Callee->getName() == "llvm.nearbyint.f128") {
                CF1(FCall, "fp128_nearbyint");
              } else if (Callee->getName() == "llvm.round.f128") {
                CF1(FCall, "fp128_round");
              } else if (Callee->getName() == "llvm.trunc.f128") {
                CF1(FCall, "fp128_trunc");
              } else if (Callee->getName() == "remquol") {
                CF2(FCall, "fp128_remquo");
              } else if (Callee->getName() == "fdiml") {
                CF2(FCall, "fp128_fdim");
              } else if (Callee->getName() == "llvm.maxnum.f128") {
                CF2(FCall, "fp128_fmax");
              } else if (Callee->getName() == "llvm.minnum.f128") {
                CF2(FCall, "fp128_fmin");
              } else if (Callee->getName() == "llvm.fma.f128") {
                CF3(FCall, "fp128_fma");
              } else if (Callee->getName() == "scalbl") {
                CF1(FCall, "fp128_scalb");
              } else if (Callee->getName() == "sqrl") {
                CF1(FCall, "fp128_sqr");
              } 
	    } else if (FCall->getOperand(0)->getType()->isFP128Ty()) {
	      if (Callee->getName() == "__fpclassifyl") {
                CF1(FCall, "fp128_fpclassifyl");
              } else if (Callee->getName() == "__signbitl") {
                CF1(FCall, "fp128_signbitl");
              } else if (Callee->getName() == "__isinfl" || Callee->getName() == "isinfl") {
                CF1(FCall, "fp128_isinfl");
              } else if (Callee->getName() == "llvm.is.fpclass.f128") {
                //fp128_finitel
		errs() << "finitel is invalid, Recommend to use fpclass.\n";
              } else if (Callee->getName() == "__isnanl" || Callee->getName() == "isnanl") {
                CF1(FCall, "fp128_isnanl");
              } else if (Callee->getName() == "__iseqsigl") {
                CF2(FCall, "fp128_iseqsigl");
              } else if (Callee->getName() == "__issignalingl") {
                CF1(FCall, "fp128_issignalingl");
              } else if (Callee->getName() == "ilogbl") {
                CF1(FCall, "fp128_ilogb");
              } else if (Callee->getName() == "lrintl") {
                CF1(FCall, "fp128_lrint");
              } else if (Callee->getName() == "llrintl") {
                CF1(FCall, "fp128_llrint");
              } else if (Callee->getName() == "lroundl") {
                CF1(FCall, "fp128_lround");
              } else if (Callee->getName() == "llroundl") {
                CF1(FCall, "fp128_llround");
              } 
	    } else if (isScanfFamily(Callee->getName())) {
	      errs() << "scanf : " << *FCall << "\n";

	      Value *FmtOp = FCall->getOperand(0);
	      errs() << "FmtOp : " << *FmtOp << "\n";

	      BitCastOperator *t = dyn_cast<BitCastOperator>(FmtOp);
	      errs() << "FmtOp BitCastOperator : " << t << "\n";
	      GlobalVariable *GV = dyn_cast<GlobalVariable>(
			      dyn_cast<BitCastOperator>(FmtOp)
			      ? dyn_cast<BitCastOperator>(FmtOp)->getOperand(0)
			      : nullptr);
	      if (!GV || !GV->isConstant()) {
	      	errs() << "GV is null.\n";
	      } else {
	      	errs() << "GV : " << *GV << "\n";
	      }


	      GlobalVariable *GV2 = getUnderlyingGlobalString(FmtOp);
	      if (!GV2) {
	      	errs() << "GV2 is null.\n";
	      } else {
	      	errs() << "GV2 : " << *GV2 << "\n";
	      }


              //for (unsigned i = 0; i < FCall->getNumOperands(); ++i) {
              //  if (FCall->getOperand(i)->getType()->isPointerTy()) { 
	      //    Value *V = FCall->getOperand(i);
	      //    errs() << "i = " << i << " operand " << *V << "\n";
	      //    //Type *Ultimate = getUltimateElementType(V);
	      //    //errs() << "Pointer " << I << " -> ultimate element: " << *Ultimate << "\n";
              //  } 
              //}

	    }
            if (Callee->getName() == "printf" &&
                Callee->getFunctionType()->isVarArg()) {
#ifdef DEBUG
              // errs() << "Call printf\n";
#endif
              // FunctionType *FT = Callee->getFunctionType();
              ////errs() << "Call type : " << *FT << "\n";
              ////errs() << "Call param num : " << FT->getNumParams() << "\n";
              ////errs() << "Call num : " << FCall->getNumOperands() << "\n";
              // bool hasFP128Pa = false;
              for (unsigned i = 0; i < FCall->getNumOperands(); ++i) {
                auto *pa = FCall->getOperand(i);
#ifdef DEBUG
                // errs() << "printf param : " << *pa << "\n";
#endif
                ////errs() << "This printf : " << *FCall << " need to deal.\n";
                if (pa->getType()->isFP128Ty()) {
                  ////errs() << "fp128\n";
                  IRBuilder<> Builder(&I);
                  Function *Fun = M.getFunction("DDToFP128");
                  if (!Fun) {
                    Fun = Function::Create(
                        FunctionType::get(Type::getFP128Ty(M.getContext()),
                                          {Type::getFP128Ty(M.getContext())}, false),
                        Function::ExternalLinkage, "DDToFP128", &M);
                  }

                  auto *newPa = Builder.CreateCall(Fun, pa);
                  newPa->setDebugLoc(I.getDebugLoc());

		  FCall->setArgOperand(i, newPa);
                  //I.replaceAllUsesWith(newPa);
                  //erase.push_back(&I);
                } 
              }
            }
          }
        } else if (auto *CI = dyn_cast<CastInst>(&I)) {
		Type *SrcType = CI->getSrcTy();
		Type *DstType = CI->getDestTy();

		//errs() << "Cast Instruction : " << I << "\n";
		//errs() << "Src Type : " << *SrcType << "\n";
		//errs() << "Dst Type : " << *DstType << "\n";
		switch (CI->getOpcode()) {
			case Instruction::FPTrunc:
				if (SrcType->isFP128Ty() && DstType->isDoubleTy()) {
					CT1(CI, "DDToFP64");
				} else if (SrcType->isFP128Ty() && DstType->isFloatTy()) {
					CT1(CI, "DDToFP32");
				} else if (SrcType->isFP128Ty() && DstType->isHalfTy()) {
				        CT1(CI, "DDToFP16");
				} else if (SrcType->isFP128Ty() && DstType->isBFloatTy()) {
				        CT1(CI, "DDToBF16");
				} else {
					//errs() << "Don't neet to change.\n";
				}
				break;

			case Instruction::FPExt:
				if (SrcType->isDoubleTy() && DstType->isFP128Ty()) {
					CT1(CI, "FP64ToDD");
				} else if (SrcType->isFloatTy() && DstType->isFP128Ty()) {
					CT1(CI, "FP32ToDD");
				} else if (SrcType->isHalfTy() && DstType->isFP128Ty()) {
				        CT1(CI, "FP16ToDD");
				} else if (SrcType->isBFloatTy() && DstType->isFP128Ty()) {
				        CT1(CI, "BF16ToDD");
				} else {
					//errs() << "Don't neet to change.\n";
				}
				break;

			case Instruction::FPToUI:
				if (SrcType->isFP128Ty() && DstType->isIntegerTy()) {
					if (auto *intTy = dyn_cast<IntegerType>(DstType)) {
						unsigned bw = intTy->getIntegerBitWidth();
						if (bw == 32) {
							CT1(CI, "DDToU32");
						} else if (bw == 64) {
							CT1(CI, "DDToU64");
						}
					}
				}
				break;

			case Instruction::FPToSI:
				if (SrcType->isFP128Ty() && DstType->isIntegerTy()) {
					if (auto *intTy = dyn_cast<IntegerType>(DstType)) {
						unsigned bw = intTy->getIntegerBitWidth();
						if (bw == 32) {
							CT1(CI, "DDToS32");
						} else if (bw == 64) {
							CT1(CI, "DDToS64");
						}
					}
				}
				break;

			case Instruction::UIToFP:
				if (SrcType->isIntegerTy() && DstType->isFP128Ty()) {
					if (auto *intTy = dyn_cast<IntegerType>(SrcType)) {
						unsigned bw = intTy->getIntegerBitWidth();
						if (bw == 32) {
							CT1(CI, "U32ToDD");
						} else if (bw == 64) {
							CT1(CI, "U64ToDD");
						}
					}
				}
				break;
			
			case Instruction::SIToFP:
				if (SrcType->isIntegerTy() && DstType->isFP128Ty()) {
					if (auto *intTy = dyn_cast<IntegerType>(SrcType)) {
						unsigned bw = intTy->getIntegerBitWidth();
						if (bw == 32) {
							CT1(CI, "S32ToDD");
						} else if (bw == 64) {
							CT1(CI, "S64ToDD");
						}
					}
				}
				break;
			
			default:
				// BitCast Trunc ZExt SExt PtrToInt IntToPtr
				break;
		}
        }
#ifdef DEBUG
        // errs() << " BB : " << BB << "\n";
#endif
      }
      //move alloca inst to head of function
      BasicBlock &EntryBlock = F.getEntryBlock(); 
      if (!Allocas.empty()) {
      	IRBuilder<> Builder(&EntryBlock, EntryBlock.getFirstInsertionPt());
	for (auto *AI : Allocas) {
	  AI->moveBefore(&*Builder.GetInsertPoint());
	  //errs() << "wait to move Allocas : " << *AI << "\n";
	}
	Allocas.clear();
      }
    }
  }

  for (unsigned i = 0; i < erase.size(); i++) {
    erase[i]->eraseFromParent();
  }
  erase.clear();
#ifdef DEBUG
  // errs() << "END M : \n" << M << "\n";
#endif
  return true;
}

Constant *convertFP128(ConstantFP *FP) {
  assert(FP->getType()->isFP128Ty() && "must be fp128");

  //llvm::SmallString<128> str;
  /* 1. 原始 128-bit 值 */
  APFloat orig128 = FP->getValueAPF();

  //orig128.toString(str);
  //errs() << "orig128 = " << str << "\n";
  //str.clear();

  /* 2. 先舍入到 double → hi */
  APFloat hi64(APFloat::IEEEdouble());
  bool ignored = false;
  orig128.convert(APFloat::IEEEdouble(), APFloat::rmNearestTiesToEven, &ignored);

  //orig128.toString(str);
  //errs() << "orig64 = " << str << "\n";
  //str.clear();

  hi64 = orig128;
  
  //hi64.toString(str);
  //errs() << "hi64 = " << str << "\n";
  //str.clear();

  /* 3. 把 hi 重新提升到 128-bit（无损）*/
  APFloat hi128(APFloat::IEEEquad());
  orig128.convert(APFloat::IEEEquad(), APFloat::rmNearestTiesToEven, &ignored);

  //orig128.toString(str);
  //errs() << "hi128 = " << str << "\n";
  //str.clear();
  
  hi128 = orig128;

  //hi128.toString(str);
  //errs() << "hi128 = " << str << "\n";
  //str.clear();

  /* 4. 在 128-bit 精度下做精确余项 */
  APFloat lo128 = FP->getValueAPF();

  //lo128.toString(str);
  //errs() << "lo128 = " << str << "\n";
  //str.clear();

  lo128.subtract(hi128, APFloat::rmNearestTiesToEven);

  //lo128.toString(str);
  //errs() << "lo128 sub = " << str << "\n";
  //str.clear();

  /* 5. 余项舍入到 double → lo */
  APFloat lo64(APFloat::IEEEdouble());
  lo128.convert(APFloat::IEEEdouble(), APFloat::rmNearestTiesToEven, &ignored);

  //lo128.toString(str);
  //errs() << "lo64 = " << str << "\n";
  //str.clear();

  lo64 = lo128;

  //lo64.toString(str);
  //errs() << "lo64 = " << str << "\n";
  //str.clear();

  //hi64.toString(str);
  //errs() << "hi64 = " << str << "\n";
  //str.clear();

  //hi64.bitcastToAPInt().toString(str, 16, false);
  //errs() << "hi64 bit = " << str << "\n";
  //str.clear();

  //lo64.toString(str);
  //errs() << "lo64 = " << str << "\n";
  //str.clear();
  //lo64.bitcastToAPInt().toString(str, 16, false);
  //errs() << "lo64 bit = " << str << "\n";
  //str.clear();

  /* 6. 返回 { double, double } */
  uint64_t hiBits = hi64.bitcastToAPInt().getZExtValue();
  uint64_t loBits = lo64.bitcastToAPInt().getZExtValue();

  //errs() << "hiBits = " << hiBits << "\n";
  //errs() << "loBits = " << loBits << "\n";
  APInt raw128(128, 0);
  raw128.insertBits(hiBits, 0, 64);
  raw128.insertBits(loBits, 64, 64);

  APFloat fp128(APFloat::IEEEquad(), raw128);
  //Constant* ret = ConstantFP::get(Type::getFP128Ty(Ctx), fp128);
  //errs() << "ret = " << *ret << "\n";
  return ConstantFP::get(Type::getFP128Ty(FP->getContext()), fp128);
}

Constant *convertConstant(Constant *C) {
  if (auto *FP = dyn_cast<ConstantFP>(C)) {
    if (FP->getType()->isFP128Ty()) return convertFP128(FP);
    return C; // 其他浮点不动
  }

  if (auto *CA = dyn_cast<ConstantArray>(C)) {
    SmallVector<Constant *, 8> Ops;
    for (Use &U : CA->operands())
      Ops.push_back(convertConstant(cast<Constant>(U.get())));
    return ConstantArray::get(CA->getType(), Ops);
  }

  if (auto *CV = dyn_cast<ConstantVector>(C)) {
    SmallVector<Constant *, 8> Ops;
    for (Use &U : CV->operands())
      Ops.push_back(convertConstant(cast<Constant>(U.get())));
    return ConstantVector::get(Ops);
  }

  if (auto *CS = dyn_cast<ConstantStruct>(C)) {
    SmallVector<Constant *, 8> Ops;
    for (unsigned i = 0; i < CS->getNumOperands(); ++i)
      Ops.push_back(convertConstant(CS->getOperand(i)));
    return ConstantStruct::get(CS->getType(), Ops);
  }

  /* ConstantExpr 里可能嵌套 fp128 */
  if (auto *CE = dyn_cast<ConstantExpr>(C)) {
    SmallVector<Constant *, 8> Ops;
    for (unsigned i = 0; i < CE->getNumOperands(); ++i)
      Ops.push_back(convertConstant(CE->getOperand(i)));
    return CE->getWithOperands(Ops);
  }

  return C; // 其余常量不动
}

PreservedAnalyses FP128ToDD::run(Module &M, ModuleAnalysisManager &) {
    errs() << ">>> Perci-Tuner: Running FP128ToDD Pass...\n";
    /* 1. 先处理全局变量初始化器 */
    for (GlobalVariable &GV : M.globals())
      if (GV.hasInitializer())
        if (Constant *New = convertConstant(GV.getInitializer())) {
          GV.setInitializer(New);
          GV.mutateType(New->getType()->getPointerTo());
        }

    /* 2. 再扫描所有函数，替换函数体里的常量 */
    for (Function &F : M)
      if (!F.isDeclaration())
        for (BasicBlock &BB : F)
          for (Instruction &I : BB)
            for (Use &U : I.operands())
              if (auto *C = dyn_cast<Constant>(U.get()))
                if (Constant *New = convertConstant(C))
                  U.set(New);

  if (!runOnModule(M))
    return PreservedAnalyses::all();
  return PreservedAnalyses::none();
}
