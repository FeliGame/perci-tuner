// @Author: Guocheng - NUDT
// Usage: Record value of result variable into PERCI_TUNER_ORIGINAL_RESULT by setenv.
// Input: IR with error pragma.
// Output: IR with removed precision pragmas.

#include "RecordResult.h"
#include "utils/ParseTypeName.hpp"
#include "utils/ParseMetaDataNode.hpp"

#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;


cl::opt<bool> IsOriginal("is-original", cl::value_desc("true or false"), cl::desc("if true, record original; otherwise check rel_err with original"), cl::init(false));


// 1. 遇到#pragma error var_name，在【当前作用域】内找到并维护var_name的声明信息(DILocalVariable)
// 2. 声明一个同类型的全局变量，并且在每次变量赋值或更新时同样的插入一个全局变量的赋值语句
// 3. 在mxpcc.sh初次运行设置IsOriginal=TRUE，在main函数结尾插入全局变量的check_result函数（输出原始十六进制结果并保存到环境变量）
// 4. 搜索期间设置IsOriginal=FALSE，在main函数结尾插入全局变量的check函数（输出结果误差）
// 5. 搜索算法捕获误差值

// 定位#pragma error，并维护变量名对应的DILocalVariable
void RecordResult::searchResultVariable(Module &M) {
    auto& context = M.getContext();
    for (auto &F : M) {
        for (auto &BB : F) {
            for (auto &I : BB) {
                if (auto *CI = dyn_cast<CallInst>(&I)) {
                    if (Function *Callee = CI->getCalledFunction()) {
                        if (Callee->getName() == "llvm.precision.error") {
                            // 获取错误变量名
                            if (auto *MD = getMetadataNode(CI, 0)) {
                                auto errors = extractStringsFromMD(MD);
                                resultVarName = errors[0].str();
                                
                                // 在当前作用域查找变量的调试信息
                                for (auto &I2 : BB) {
                                    if (auto *DDI = dyn_cast<DbgDeclareInst>(&I2)) {
                                        if (auto *DV = dyn_cast<DILocalVariable>(DDI->getVariable())) {
                                            if (DV->getName() == resultVarName) {
                                                resultVar = DV;
                                                // 添加类型转换
                                                Type* llvmType = convertDITypeToLLVMType(DV->getType(), context);
                                                if (!llvmType) {
                                                    errs() << "Failed to convert DIType to LLVM Type\n";
                                                    return;
                                                }
                                                resultVarType = llvmType;
                                                errs() << "result var: [" << resultVarName << "] of type " << parseDITypeName(DV->getType()) << '\n';
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void RecordResult::createGlobalRecord(Module &M) {
    if (!resultVarType) return;
    // 已经存在该全局变量就不重复创建
    globalRecordVar = M.getGlobalVariable("__pt_result");
    if (globalRecordVar) return;
    globalRecordVar = new GlobalVariable(
        M, 
        resultVarType,  // 使用转换后的LLVM Type
        false,
        GlobalValue::ExternalLinkage,
        Constant::getNullValue(resultVarType),
        "__pt_result"
    );
    
    // TODO: 应当改为分析所有resultVar的引用结果，如果作为左值，则赋值到全局变量
    // 目前存在bug：同名变量会被同样识别
    for (auto &F : M) {
        for (auto &BB : F) {
            for (auto &I : BB) {
                if (auto *SI = dyn_cast<StoreInst>(&I)) {
                    if (SI->getPointerOperand()->getName() == resultVarName) {
                        IRBuilder<> Builder(SI->getNextNode());
                        Builder.CreateStore(SI->getValueOperand(), globalRecordVar);
                    }
                }
            }
        }
    }
}

// 在main函数结尾插入全局变量的check_result函数
void RecordResult::insertPrintFP(Module &M) {
    if (!IsOriginal || !globalRecordVar) return;
    
    Function *MainFunc = M.getFunction("main");
    if (!MainFunc) return;
    
    auto& context = M.getContext();
    // 找到main函数的返回指令
    for (auto &BB : *MainFunc) {
        if (auto *RI = dyn_cast<ReturnInst>(BB.getTerminator())) {
            IRBuilder<> Builder(RI);
            
            // 根据变量类型选择对应的printf函数
            std::string funcName;
            Type* argTy = nullptr;
            
            if (resultVarType->isHalfTy()) {
                funcName = "printf_fp16";
                argTy = Type::getHalfTy(context);
            } else if (resultVarType->isFloatTy()) {
                funcName = "printf_fp32";
                argTy = Type::getFloatTy(context);
            } else if (resultVarType->isDoubleTy()) {
                funcName = "printf_fp64";
                argTy = Type::getDoubleTy(context);
            } else if (resultVarType->isFP128Ty()) {
                funcName = "printf_fp128";
                argTy = Type::getFP128Ty(context);
            } else {
                errs() << "Unsupported type for printf: " << parseTypeName(resultVarType) << "\n";
                return; // 不支持的类型
            }
            auto *PrintfTy = FunctionType::get(
                Type::getVoidTy(context),
                {argTy},
                false
            );
            auto PrintfFunc = M.getOrInsertFunction(funcName, PrintfTy);
            
            // 加载全局变量值
            Value *ResultValue = Builder.CreateLoad(resultVarType, globalRecordVar);
            
            // 调用对应的printf函数
            Builder.CreateCall(PrintfFunc, {ResultValue});
        }
    }
}

void RecordResult::insertCheckRE(Module &M) {
    if (IsOriginal || !globalRecordVar) return;
    
    Function *MainFunc = M.getFunction("main");
    if (!MainFunc) return;

    auto& context = M.getContext();
    // 找到main函数的返回指令
    for (auto &BB : *MainFunc) {
        if (auto *RI = dyn_cast<ReturnInst>(BB.getTerminator())) {
            IRBuilder<> Builder(RI);
            
            // 根据变量类型选择对应的check函数
            std::string funcName;
            Type* argTy = nullptr;
            
            if (resultVarType->isHalfTy()) {
                funcName = "printf_RE_fp16";
                argTy = Type::getHalfTy(context);
            } else if (resultVarType->isFloatTy()) {
                funcName = "printf_RE_fp32";
                argTy = Type::getFloatTy(context);
            } else if (resultVarType->isDoubleTy()) {
                funcName = "printf_RE_fp64";
                argTy = Type::getDoubleTy(context);
            } else if (resultVarType->isFP128Ty()) {
                funcName = "printf_RE_fp128";
                argTy = Type::getFP128Ty(context);
            } else {
                errs() << "Unsupported type for check: " << parseTypeName(resultVarType) << "\n";
                return; // 不支持的类型
            }
            auto *CheckTy = FunctionType::get(
                Type::getVoidTy(context),
                {argTy},
                false
            );
            auto CheckFunc = M.getOrInsertFunction(funcName, CheckTy);
            
            // 加载全局变量值
            Value *ResultValue = Builder.CreateLoad(resultVarType, globalRecordVar);
            
            // 调用对应的check函数
            Builder.CreateCall(CheckFunc, {ResultValue});
        }
    }
}

Type* RecordResult::convertDITypeToLLVMType(DIType* diType, LLVMContext& context) {
    if (!diType) return nullptr;
    
    if (auto* basicType = dyn_cast<DIBasicType>(diType)) {
        StringRef name = basicType->getName();
        if (name == "__fp16") return Type::getHalfTy(context);
        if (name == "float") return Type::getFloatTy(context);
        if (name == "double") return Type::getDoubleTy(context);
        if (name == "long double") return Type::getFP128Ty(context);
        errs() << "convert [" << name << "] from DIType to LLVMType ...\n";
    }
    // TODO: 添加其他类型...
    return nullptr;
}

void RecordResult::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();
}

PreservedAnalyses RecordResult::run(Module &M, ModuleAnalysisManager &MAM) {
    errs() << ">>> Perci-Tuner: Running RecordResult Pass...\n";
    searchResultVariable(M);
    createGlobalRecord(M);
    if (IsOriginal) {
        insertPrintFP(M);
    } else {
        insertCheckRE(M);
    }
    return PreservedAnalyses::none();  // has changed IR
}
