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


cl::opt<string> TypeConfigName("type-config", cl::value_desc("filename"), cl::desc("Type Config file"), cl::init("config.json"));

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
    std::ifstream configFile(TypeConfigName.c_str());
    if (!configFile.is_open()) {
        errs() << "Failed to open " << TypeConfigName << "\n";
        return;
    }

    std::string configContent((std::istreambuf_iterator<char>(configFile)), std::istreambuf_iterator<char>());
    configFile.close();
    
    config = nlohmann::json::parse(configContent);
}

void ChangePrecision::changeVariablePrecision(DbgDeclareInst *DDI, Type *newType, LLVMContext& context) {
    // 获取原始变量的分配指令
    Value *var = DDI->getAddress();
    if (auto alloca = dyn_cast<AllocaInst>(var)) {
        // 更改分配指令的类型
        alloca->mutateType(newType);

        // // 获取原始DILocalVariable对象
        // DILocalVariable *originalDV = DDI->getVariable();

        // // 创建新的DILocalVariable对象
        // DILocalVariable *newDV = new (context) DILocalVariable(
        //     originalDV->getScope(),            // Scope
        //     originalDV->getName(),            // Name
        //     originalDV->getFile(),            // File
        //     originalDV->getLine(),            // Line
        //     cast<DIType>(newType),            // Type
        //     originalDV->getArg(),             // Arg
        //     originalDV->getFlags(),           // Flags
        //     originalDV->getAlignInBits()      // AlignInBits
        // );
        // // 更新调试信息指令以反映新的类型
        // DDI->setVariable(newDV);
    }
    // 如果需要，添加其他代码来处理不同类型的变量
}


bool ChangePrecision::runOnFunction(Function &F) {
    auto &context = F.getContext();
    auto *doubleType = Type::getDoubleTy(context);
    auto functionConfig = config["local"][demangle(F.getName())];

    for (auto &BB : F) {
        for (auto &I : BB) {
           if (auto *DDI = dyn_cast<DbgDeclareInst>(&I)) {
                if (auto *DV = dyn_cast<DILocalVariable>(DDI->getVariable())) {
                    auto varName = DV->getName();
                    // 查找 JSON 中是否有匹配的变量名称和类型更改配置
                    if (functionConfig.contains(varName)) {
                        auto typeName = functionConfig[varName]["type"];
                        if (typeName == "float" || typeName == "double" || typeName == "long double") {
                            errs() << F.getName() << " found " << varName.str() << ": " << typeName.dump(4) << '\n';
                            // changeVariablePrecision(DDI, doubleType, context);
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