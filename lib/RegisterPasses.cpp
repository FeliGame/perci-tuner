// @Author: Guocheng - NUDT
// Register all pass plugins

// Ref: llvm-project/llvm/Examples/Bye.cpp

#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include "SearchFunctions.h"
#include "CreatePrecisionConfig.h"
#include "ChangePrecision.h"
#include "IRTiming.h"

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
						if (Name == "search_functions")
						{
							PM.addPass(SearchFunctions());
							return true;
						}
						if (Name == "config")
						{
							PM.addPass(CreatePrecisionConfig());
							return true;
						}
						if (Name == "change_precision")
						{
							PM.addPass(ChangePrecision());
							return true;
						}
						if (Name == "ir_timing")
						{
							PM.addPass(IRTiming());
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