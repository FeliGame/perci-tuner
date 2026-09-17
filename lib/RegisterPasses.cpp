// @Author: Guocheng - NUDT
// Register all pass plugins

// Ref: llvm-project/llvm/Examples/Bye.cpp

#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#if LLVM_VERSION_MAJOR >= 19
#include "llvm/Plugins/PassPlugin.h"
#else
#include "llvm/Passes/PassPlugin.h"
#endif
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include "PrintConfig.h"
#include "ChangeVariableDecl.h"
#include "ChangeMathCall.h"
#include "IRTiming.h"
#include "MergeMultiVersion.h"
#include "RecordResult.h"
#include "RemovePragma.h"
#include "FP128ToDD.h"

using namespace llvm;

/* New PM Registration */
llvm::PassPluginLibraryInfo getPerciTunerPassPluginInfo()
{
	return {LLVM_PLUGIN_API_VERSION, "PerciTuner", LLVM_VERSION_STRING,
			[](PassBuilder &PB)
			{
				// PB.registerAnalysisRegistrationCallback(
				// 	[](ModuleAnalysisManager &MAM) {
				// 		MAM.registerPass([] { 
				// 			return IRTiming(); 
				// 		});
				// 	}
				// );
				// PB.registerVectorizerStartEPCallback(
				//     [](llvm::ModulePassManager &PM, OptimizationLevel Level) {
				//       PM.addPass(???);
				//     });
				PB.registerPipelineParsingCallback(
					[](StringRef Name, llvm::ModulePassManager &PM,
					   ArrayRef<llvm::PassBuilder::PipelineElement>)
					{
						if (Name == "print_config")
						{
							PM.addPass(PrintConfig());
							return true;
						}
						if (Name == "change_variable_decl")
						{
							PM.addPass(ChangeVariableDecl());
							return true;
						}
						if (Name == "change_math_call")
						{
							PM.addPass(ChangeMathCall());
							return true;
						}
						if (Name == "ir_timing")
						{
							PM.addPass(IRTiming());
							return true;
						}
						if (Name == "merge_multi")
						{
							PM.addPass(MergeMultiVersion());
							return true;
						}
						if (Name == "record_result")
						{
							PM.addPass(RecordResult());
							return true;
						}
						if (Name == "remove_pragma")
						{
							PM.addPass(RemovePragma());
							return true;
						}
						if (Name == "fp128_to_dd")
						{
							PM.addPass(FP128ToDD());
							return true;
						}
						return false;
					});
			}};
}

#ifndef LLVM_CREATE_PERCI_TUNER_PASS_LINK_INTO_TOOLS
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo()
{
	return getPerciTunerPassPluginInfo();
}
#endif