#include "CreateCallDependency.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"

#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/ValueSymbolTable.h>

using namespace std;
using namespace llvm;


cl::opt<string> CallEntryName("call-entry", cl::value_desc("name"), cl::desc("Name of program entry"), cl::init("main"));

bool CreateCallDependency::runOnFunction(Function &F) {
    errs() << "Find function: " << F.getName() << "\n";
    
    if (F.getName() != CallEntryName) {
        return false;
    }

    vector<string> CalledFunctions;

    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *call = dyn_cast<CallInst>(&I)) {
                // 获取被调用函数的名称
                auto *Callee = call->getCalledFunction();
                if (Callee) {
                    CalledFunctions.push_back(Callee->getName().str());
                }
            }
        }
    }

    // 输出调用的函数名称
    errs() << "Called Functions in " << F.getName() << ":\n";
    for (const auto &FuncName : CalledFunctions) {
        errs() << " - " << FuncName << "\n";
    }

    return false;
}

bool CreateCallDependency::runOnModule(Module &module) {
    for(Module::iterator f = module.begin(), fe = module.end(); f != fe; f++) {
        runOnFunction(*f);
    }
    return false;  // hasn't changed IR
}  

void CreateCallDependency::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();
}

PreservedAnalyses CreateCallDependency::run(Module &M, ModuleAnalysisManager &) {
    if (!runOnModule(M))
        return PreservedAnalyses::all();
    return PreservedAnalyses::none();
}