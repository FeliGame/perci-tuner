// @Author: Guocheng - NUDT
// Usage: Change variables' precision according to json.
// Optional FLAG: 
// Input: Variable new type config
// Output: Precision-Changed IR

#include "ChangePrecision.h"
#include "utils/ParseTypeName.hpp"

#include "nlohmann/json.hpp"
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/ValueSymbolTable.h>
#include <llvm/Demangle/Demangle.h>
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/APSInt.h>

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
    auto align = Align(newType->getPrimitiveSizeInBits() / 8);
    if (auto alloca = dyn_cast<AllocaInst>(var)) {
        errs() << "Changing AllocInst: " << *alloca << "\n";
        alloca->setAllocatedType(newType);
        alloca->setAlignment(align);
        errs() << "Changed AllocInst: " << *alloca << "\n";

        vector<Instruction*> instToErase;  // Inst to be erased later

        for (User *user : alloca->users()) {
            if (auto store = dyn_cast<StoreInst>(user)) {
                // 获取原始的赋值
                Value *oldValue = store->getOperand(0);

                // 将原始的赋值转换为新的类型
                Value *newValue = nullptr;
                // only converting float/double/long double
                if (oldValue->getType()->isFloatingPointTy() && newType->isFloatingPointTy()) {
                    auto valueAPF = dyn_cast<ConstantFP>(oldValue)->getValueAPF();
                    if (newType->isFloatTy()) {
                        newValue = ConstantFP::get(newType, valueAPF.convertToFloat());
                    } else if (newType->isDoubleTy()) {
                        newValue = ConstantFP::get(newType, valueAPF.convertToDouble());
                    // } else if (newType->isFP128Ty()) {
                    //     newValue = ConstantFP::get(newType, valueAPF.convert(APFloat::IEEEquad(), APFloat::rmNearestTiesToEven));
                    } else {
                        errs() << "Unsupported new type: " << *newType << "\n";
                        continue;
                    }
                } else {
                    errs() << "Unsupported old type: " << *oldValue->getType() << "\n";
                    continue;
                }

                store->setOperand(0, newValue);
                store->setAlignment(align);
            }
            else if (auto load = dyn_cast<LoadInst>(user)) {
                Type *oldType = load->getType();
                if (oldType == newType) continue;

                auto *newLoad = new LoadInst(newType, load->getPointerOperand(), "", load);
                newLoad->setAlignment(align);
                if (MDNode *N = load->getMetadata("dbg")) {
                    newLoad->setMetadata("dbg", N);
                }

                load->replaceAllUsesWith(newLoad);
                instToErase.emplace_back(load);    // erase later to avoid broken iterator
            }
        }

        for (auto* inst : instToErase) {
            inst->eraseFromParent();
        }
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

                            // If new type is same as before, skip
                            if (typeName == parseDITypeName(DV->getType())) continue;
                            if (typeName == "float") {
                                targetType = Type::getFloatTy(context);
                            } else if (typeName == "double") {
                                targetType = Type::getDoubleTy(context);
                            } else if (typeName == "long double") {
                                targetType = Type::getFP128Ty(context);
                            } else if (typeName == "int") {
                                targetType = Type::getInt32Ty(context);
                            } else if (typeName == "long") {
                                targetType = Type::getInt64Ty(context);
                            } else if (typeName == "long long") {
                                targetType = Type::getInt128Ty(context);
                            } else {  // TODO: Add more type such as const/array/pointer...
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