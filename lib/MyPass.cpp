#include "MyPass.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"

#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/ValueSymbolTable.h>

using namespace llvm;


bool MyPass::runOnFunction(Function &F) {
    errs() << "Running My LLVM Pass on " << F.getName() << "\n";
    // Your pass logic here
    return false;
}

bool MyPass::runOnModule(Module &module) {
    for(Module::iterator f = module.begin(), fe = module.end(); f != fe; f++) {
        runOnFunction(*f);
    }
    return false;  // hasn't changed IR
}  

void MyPass::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();
}

// Legacy PM Registration
// char MyPass::ID = 0;
// RegisterPass<MyPass> X("mypass", "My LLVM Pass", false, false);

PreservedAnalyses MyPass::run(Module &M, ModuleAnalysisManager &) {
    if (!runOnModule(M))
        return PreservedAnalyses::all();
    return PreservedAnalyses::none();
}