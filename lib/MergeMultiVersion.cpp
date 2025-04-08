// @Author: Guocheng - NUDT
// Usage: Merge two version of IR's selected function into branches.
// Input: Filename of the other version of IR, Merge parameters: --ir-multi=Another IR Name --function-multi=Function name
// Current module must link to mvselect.bc
// Output: Merged IR

#include "MergeMultiVersion.h"
#include "utils/ParseTypeName.hpp"

#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/ValueSymbolTable.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Linker/IRMover.h>

#include <fstream>

using namespace std;
using namespace llvm;

cl::opt<string> MultiVersionIRName("ir-multi", cl::value_desc("filename"), cl::desc("The other IR to be merged"), cl::init("optimized_output.bc"));
cl::opt<string> MultiVersionFunction("function-multi", cl::value_desc("function name"), cl::desc("Function need multi-version"), cl::init("main"));

bool MergeMultiVersion::runOnModule(Module &module)
{
    auto &Context = module.getContext();

    // Read the other IR module & specified function
    SMDiagnostic Err;
    auto otherModule = parseIRFile(MultiVersionIRName, Err, Context);
    if (!otherModule)
    {
        errs() << "Error reading IR file: " << MultiVersionIRName << "\n";
        return false;
    }

    auto findFunctionByName = [&](Module &module, StringRef Name) -> Function* {
        for (auto &F : module) {
            if (F.getName() == Name) {
                return &F;
            }
        }
        return nullptr;
    };

    auto *CurrentFunc = findFunctionByName(module, MultiVersionFunction);
    auto *OtherFunc = findFunctionByName(*otherModule, MultiVersionFunction);
    OtherFunc->setName(MultiVersionFunction + ".cloned");

    if (!CurrentFunc || !OtherFunc)
    {
        errs() << "Function not found in module\n";
        return false;
    }

    // Move otherFunc to current module
    IRMover Mover(module);
    SmallVector<GlobalValue *, 8> ToClone;
    ToClone.push_back(OtherFunc);
    auto HandleError = [](const Twine &Msg)
    { return Error::success(); };
    if (Error Err = Mover.move(std::move(otherModule), ToClone, [](GlobalValue &GV, std::function<void(GlobalValue &)> Materialize)
                               { Materialize(GV); }, HandleError))
    {
        errs() << "IRMover error: " << Err << "\n";
        return false;
    }

    // Update function pointers
    CurrentFunc = findFunctionByName(module, MultiVersionFunction);
    OtherFunc = findFunctionByName(module, MultiVersionFunction + ".cloned");

    // Insert mvselect's declaration
    auto *intType = Type::getInt32Ty(Context);
    auto mvSelectCallee = module.getOrInsertFunction(
        "mvselect",
        FunctionType::get(intType, {}, false));
    mvSelectFunction = cast<Function>(mvSelectCallee.getCallee());

    // Iterate over all call instructions in the module
    for (auto &F : module)
    {
        // Skip cloned function itself
        if (F.isDeclaration() || F.getName() == "mvselect" || F.getName().startswith(MultiVersionFunction))
        {
            continue;
        }

        for (auto &BB : F)
        {
            // Cache to avoid destroy iterators
            SmallVector<CallInst *, 16> ToProcess;

            for (auto &I : BB)
            {
                if (auto *CI = dyn_cast<CallInst>(&I))
                {
                    Function *Callee = CI->getCalledFunction();
                    if (Callee && Callee->getName() == MultiVersionFunction)
                    {
                        ToProcess.push_back(CI);
                    }
                }
            }

            for (auto *CI : ToProcess)
            {
                if (processed.count(CI))
                    continue;
                processed.insert(CI);

                BasicBlock *OrigBB = CI->getParent();
                BasicBlock *PostBB = OrigBB->splitBasicBlock(CI->getNextNode(), "post.call");

                // 3 New BB, insert before PostBB
                BasicBlock *CondBB = BasicBlock::Create(Context, "mv.cond", &F, PostBB);
                BasicBlock *OrigCallBB = BasicBlock::Create(Context, "orig.call", &F, PostBB);
                BasicBlock *ClonedCallBB = BasicBlock::Create(Context, "cloned.call", &F, PostBB);

                // OrigBB->CondBB
                OrigBB->getTerminator()->eraseFromParent();
                BranchInst::Create(CondBB, OrigBB);

                // CondBB->OrigCallBB/ClonedCallBB
                IRBuilder<> CondBuilder(CondBB);
                Value *Cond = CondBuilder.CreateCall(mvSelectFunction, {}, "mvselect");
                // If Cond == 0, go to OrigCallBB, otherwise go to ClonedCallBB
                Value *Cmp = CondBuilder.CreateICmpEQ(Cond, ConstantInt::get(Cond->getType(), 0));
                CondBuilder.CreateCondBr(Cmp, OrigCallBB, ClonedCallBB);

                // OrigCallBB->PostBB
                IRBuilder<> OrigBuilder(OrigCallBB);
                SmallVector<Value *, 8> OrigArgs;
                for (auto &Arg : CI->args())
                {
                    OrigArgs.push_back(Arg.get());
                }
                CallInst *OrigCall = OrigBuilder.CreateCall(cast<Function>(CurrentFunc), OrigArgs);
                OrigCall->setDebugLoc(CI->getDebugLoc());
                OrigBuilder.CreateBr(PostBB);

                // ClonedCallBB->PostBB
                IRBuilder<> ClonedBuilder(ClonedCallBB);
                SmallVector<Value *, 8> ClonedArgs;
                for (auto &Arg : CI->args())
                {
                    ClonedArgs.push_back(Arg.get());
                }
                CallInst *ClonedCall = ClonedBuilder.CreateCall(cast<Function>(OtherFunc), ClonedArgs);
                ClonedCall->setDebugLoc(CI->getDebugLoc());
                ClonedBuilder.CreateBr(PostBB);

                // PHI in PostBB
                if (!CI->getType()->isVoidTy())
                {
                    IRBuilder<> PhiBuilder(PostBB, PostBB->begin());
                    PHINode *Phi = PhiBuilder.CreatePHI(CI->getType(), 2);
                    assert(OrigCall->getType() == CI->getType() && "Type mismatch!");
                    assert(ClonedCall->getType() == CI->getType() && "Type mismatch!");
                    Phi->addIncoming(OrigCall, OrigCallBB);
                    Phi->addIncoming(ClonedCall, ClonedCallBB);
                    CI->replaceAllUsesWith(Phi);
                }
                CI->eraseFromParent();

                processed.insert(OrigCall);
                processed.insert(ClonedCall);
            }
        }
    }

    return true; // IR has been modified
}

void MergeMultiVersion::getAnalysisUsage(AnalysisUsage &AU) const
{
    AU.setPreservesAll(); // hasn't changed IR
}

PreservedAnalyses MergeMultiVersion::run(Module &M, ModuleAnalysisManager &)
{
    errs() << ">>> Perci-Tuner: Running MergeMultiVersion Pass...\n";
    if (!runOnModule(M))
        return PreservedAnalyses::all();
    return PreservedAnalyses::none();
}
