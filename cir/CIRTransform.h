//===- CIRTransform.h - Precision transform on CIR -------------*- C++ -*-===//
//
// CIR counterpart of lib/ChangeVariableDecl.cpp + lib/ChangeMathCall.cpp.
// Reads config.json and rewrites variable/global/struct precision and math
// call precision inside a cir.module. Function parameter / return signature
// changes are intentionally not implemented in the first version: config
// entries with isParam=true or name "ret." are skipped.
//
//===----------------------------------------------------------------------===//

#ifndef PERCI_CIR_TRANSFORM_H
#define PERCI_CIR_TRANSFORM_H

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

namespace perci {

/// MLIR module pass: transform CIR according to --variable-config /
/// --function-config JSON files.
struct CIRTransform : public mlir::OperationPass<mlir::ModuleOp> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(CIRTransform)

  CIRTransform()
      : mlir::OperationPass<mlir::ModuleOp>(
            mlir::TypeID::get<CIRTransform>()) {}

  llvm::StringRef getName() const override { return "CIRTransform"; }
  std::unique_ptr<mlir::Pass> clonePass() const override {
    return std::make_unique<CIRTransform>();
  }

  void runOnOperation() override;
};

std::unique_ptr<mlir::Pass> createCIRTransformPass();

} // namespace perci

#endif // PERCI_CIR_TRANSFORM_H
