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

std::set<std::string> CalledFunctions;


void printCalledFunctions(Function* callee, int depth) {
    for (int i = 0; i < depth; ++i)
    {
        errs() << "\t";
    }
    errs() << callee->getName() << "\n";
}

// Enter function calls recursively
void collectCalledFunctions(Function &F, std::set<Function*> &visited, int depth) {
    if (!visited.insert(&F).second) {
        // 如果已经访问过这个函数，就返回
        return;
    }
    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *call = dyn_cast<CallInst>(&I)) {
                auto *callee = call->getCalledFunction();
                if (callee && !callee->isIntrinsic()) { // 排除内建函数
                    CalledFunctions.insert(callee->getName().str());
                    printCalledFunctions(callee, depth);
                    collectCalledFunctions(*callee, visited, depth + 1); // 递归调用
                }
            }
        }
    }
}

bool CreateCallDependency::runOnFunction(Function &F) {
    errs() << "Find function: " << F.getName() << "\n";
    
    if (F.getName() != CallEntryName) {
        return false;
    }

    std::set<Function*> visited;
    collectCalledFunctions(F, visited, 0);

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