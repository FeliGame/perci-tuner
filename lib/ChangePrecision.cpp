// @Author: Guocheng - NUDT
// Usage: Change variables' precision according to json.
// Optional FLAG: 
// Input: Variable new type config
// Output: Precision-Changed IR

#include "ChangePrecision.h"

#include "nlohmann/json.hpp"
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/ValueSymbolTable.h>
#include <llvm/Demangle/Demangle.h>

#include <fstream>

using namespace std;
using namespace llvm;


cl::opt<string> TargetConfigName("target-config", cl::value_desc("filename"), cl::desc("Target Type Config"), cl::init("config.json"));

void ChangePrecision::parseIncludedFunctions() {
    ifstream inFile("searched_functions.txt");
    string name;

    if (!inFile) {
        errs() << "Unable to open searched_functions.txt\n";
        exit(1);
    }

    while(inFile >> name) {
        includedFunctions.insert(name);
    }
}

// 【Temporary】
void ChangePrecision::parseTypeConfig() {
    std::ifstream configFile(TargetConfigName.c_str());
    if (!configFile.is_open()) {
        errs() << "Failed to open " << TargetConfigName << "\n";
        return;
    }

    std::string configContent((std::istreambuf_iterator<char>(configFile)), std::istreambuf_iterator<char>());
    configFile.close();
    
    config = nlohmann::json::parse(configContent);
}

void ChangePrecision::changeVariablePrecision(DbgDeclareInst *DDI, Type *newType) {
    Value *var = DDI->getAddress();
    if (auto alloca = dyn_cast<AllocaInst>(var)) {
        errs() << "Changing AllocInst: " << *alloca << "\n";
        alloca->setAllocatedType(newType);
        alloca->setAlignment(Align(newType->getPrimitiveSizeInBits() / 8));
        errs() << "Changed AllocInst: " << *alloca << "\n";

        // // TODO: 更新所有相关的store指令的类型
        // for (User *user : alloca->users()) {
        //     if (auto store = dyn_cast<StoreInst>(user)) {
        //         // 获取原始的赋值
        //         Value *oldValue = store->getOperand(0);

        //         // 将原始的赋值转换为新的类型
        //         Value *newValue = nullptr;
        //         if (oldValue->getType()->isFloatingPointTy()) {
        //             newValue = ConstantFP::get(newType, dyn_cast<ConstantFP>(oldValue)->getValueAPF().convertToFloat());
        //         } else if (oldValue->getType()->isIntegerTy()) {
        //             newValue = ConstantInt::get(newType, dyn_cast<ConstantInt>(oldValue)->getZExtValue());
        //         } else {
        //             errs() << "Unsupported type: " << *oldValue->getType() << "\n";
        //             continue;
        //         }

        //         // 更新store指令的操作数
        //         store->setOperand(0, newValue);
        //     }
        // }
    }
}


bool ChangePrecision::runOnFunction(Function &F) {
    auto &context = F.getContext();
    auto functionConfig = config["local"][demangle(F.getName())];

    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *DDI = dyn_cast<DbgDeclareInst>(&I)) {
                if (auto *DV = dyn_cast<DILocalVariable>(DDI->getVariable())) {
                    auto varName = DV->getName();
                    unsigned lineNo = DDI->getDebugLoc().getLine();

                    // TODO: Iterate each var to match, can be more efficient
                    for (auto &configEntry : functionConfig) {
                        if (configEntry["name"] == varName && configEntry["line"] == std::to_string(lineNo)) {
                            Type *targetType = nullptr;
                            auto typeName = configEntry["type"].get<string>();
                            if(typeName == "float") {
                                targetType = Type::getFloatTy(context);
                            } else if(typeName == "double") {
                                targetType = Type::getDoubleTy(context);
                            } else if(typeName == "long double") {
                                targetType = Type::getFP128Ty(context);
                            } else {  // TODO: Add more type
                                errs() << "Unknown type: " << typeName << "\n";
                                continue;
                            }

                            errs() << F.getName() << ": " << varName.str() << " at line " << lineNo << typeName << '\n';
                            changeVariablePrecision(DDI, targetType);
                            break;  // found & changed
                        }
                    }
                }
            }
        }
    }
    return true;
}

bool ChangePrecision::runOnModule(Module &module) {
    parseIncludedFunctions();
    parseTypeConfig();

    for(Module::iterator f = module.begin(), fe = module.end(); f != fe; f++) {
        string name = f->getName().str();
        if(!f->isDeclaration() && includedFunctions.find(name) != includedFunctions.end()) {
            runOnFunction(*f);
        }
    }
    return true;  // has changed IR
}  

void ChangePrecision::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();  // hasn't changed IR
}

PreservedAnalyses ChangePrecision::run(Module &M, ModuleAnalysisManager &) {
    errs() << ">>> Perci-Tuner: Running ChangePrecision Pass...\n";
    if (!runOnModule(M))
        return PreservedAnalyses::all();
    return PreservedAnalyses::none();
}