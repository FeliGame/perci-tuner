//===- CIRPrintConfig.h - Print floating-point config from CIR -*- C++ -*-===//
//
// CIR counterpart of lib/PrintConfig.cpp. Walks a cir.module and emits
// config.json describing floating-point variables, globals and math calls.
//
//===----------------------------------------------------------------------===//

#ifndef PERCI_CIR_PRINT_CONFIG_H
#define PERCI_CIR_PRINT_CONFIG_H

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

namespace perci {

/// MLIR module pass: scan CIR and write config.json in the current directory.
struct CIRPrintConfig : public mlir::OperationPass<mlir::ModuleOp> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(CIRPrintConfig)

  CIRPrintConfig()
      : mlir::OperationPass<mlir::ModuleOp>(
            mlir::TypeID::get<CIRPrintConfig>()) {}

  llvm::StringRef getName() const override { return "CIRPrintConfig"; }
  std::unique_ptr<mlir::Pass> clonePass() const override {
    return std::make_unique<CIRPrintConfig>();
  }

  void runOnOperation() override;
};

std::unique_ptr<mlir::Pass> createCIRPrintConfigPass();

} // namespace perci

#endif // PERCI_CIR_PRINT_CONFIG_H
