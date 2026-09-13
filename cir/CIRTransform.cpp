//===- CIRTransform.cpp - Precision transform on CIR ---------------------===//
//
// Reads config.json (--variable-config / --function-config) and rewrites a
// cir.module in two phases:
//   1. math call precision (calls section)         [M4]
//   2. variable / global / struct precision        (this file)
//
// Function parameter and return *signature* changes are intentionally not
// implemented: config entries with isParam=true or name "ret." are skipped.
//
// Variable rewriting strategy: replace the cir.alloca storage type, then run
// a fixpoint that repairs memory boundary ops:
//   * cir.store inserts an fp widening/narrowing cast on the stored value
//   * cir.load is recreated with the new pointee type and propagates the
//     new type to its users
//   * cir.binop / cir.unary follow the "highest precision operand wins"
//     rule of lib/ChangeVariableDecl.cpp (fp constants do not lock the
//     type; pre-existing fp casts are looked through)
//   * cir.return casts back to the (unchanged) function result type
//
//===----------------------------------------------------------------------===//

#include "CIRTransform.h"
#include "CIRConfig.hpp"

#include "llvm/Support/raw_ostream.h"

#include "mlir/IR/IRMapping.h"

#include <fstream>
#include <map>

using namespace mlir;

namespace perci {

namespace {

// ---------------------------------------------------------------------------
// Config loading
// ---------------------------------------------------------------------------
struct TransformConfig {
  std::string contents; // json strings reference this buffer
  json::Value root{nullptr};
  // file:line:column -> local variable entry
  std::map<std::string, json::Value *> localByKey;

  bool load(llvm::StringRef path) {
    std::ifstream f(path.str());
    if (!f.is_open())
      return false;
    contents.assign((std::istreambuf_iterator<char>(f)),
                    std::istreambuf_iterator<char>());
    auto parsed = json::parse(llvm::StringRef(contents));
    if (!parsed)
      return false;
    root = std::move(*parsed);

    if (json::Object *rootObj = root.getAsObject())
      if (json::Value *local = rootObj->get("local"))
        if (auto *obj = local->getAsObject())
        for (auto &[funcName, arrVal] : *obj)
          if (auto *arr = arrVal.getAsArray())
            for (json::Value &entry : *arr) {
              auto *e = entry.getAsObject();
              if (!e)
                continue;
              auto file = e->getString("file");
              auto line = e->getNumber("line");
              auto col = e->getNumber("column");
              if (!file || !line || !col)
                continue;
              std::string key = makeKey(*file, (unsigned)*line, (unsigned)*col);
              localByKey[key] = &entry;
            }
    return true;
  }
};

// ---------------------------------------------------------------------------
// Variable precision transformer
// ---------------------------------------------------------------------------
class VariableTransformer {
public:
  VariableTransformer(cir::FuncOp func, const TransformConfig &cfg)
      : func(func), cfg(cfg), ctx(func.getContext()) {}

  bool run() {
    if (!changeAllocas())
      return false;
    repairFixpoint();
    eraseDeadOps();
    return true;
  }

private:
  cir::FuncOp func;
  const TransformConfig &cfg;
  MLIRContext *ctx;
  // Casts created by this pass; these are never "looked through" when
  // inferring a binop's highest precision.
  DenseSet<Operation *> insertedCasts;
  // Ops erased during the current fixpoint sweep (address-only liveness set;
  // it never dereferences stored pointers).
  DenseSet<Operation *> erasedOps;

  // ---- cast creation ----------------------------------------------------

  Value castTo(Value v, Type dst, OpBuilder &b) {
    Type src = v.getType();
    if (src == dst)
      return v;
    cir::CastKind kind;
    bool srcFP = isCIRFPType(src), dstFP = isCIRFPType(dst);
    if (srcFP && dstFP)
      kind = cir::CastKind::floating;
    else if (isCIRIntType(src) && dstFP)
      kind = cir::CastKind::int_to_float;
    else if (srcFP && isCIRIntType(dst))
      kind = cir::CastKind::float_to_int;
    else if (isCIRIntType(src) && isCIRIntType(dst))
      kind = cir::CastKind::integral;
    else
      return {}; // unsupported (e.g. pointer conversions)
    auto cast = b.create<cir::CastOp>(v.getLoc(), dst, kind, v);
    insertedCasts.insert(cast);
    return cast;
  }

  // ---- alloca replacement ----------------------------------------------

  bool changeAllocas() {
    bool changed = false;
    SmallVector<cir::AllocaOp> targets;
    SmallVector<Type> targetTypes;

    func.walk([&](cir::AllocaOp alloca) {
      llvm::StringRef name = alloca.getName();
      if (name.empty() || name == "__retval")
        return;
      SrcLoc loc = getSrcLoc(alloca.getLoc());
      auto it = cfg.localByKey.find(makeKey(loc.file, loc.line, loc.column));
      if (it == cfg.localByKey.end())
        return;
      auto *e = it->second->getAsObject();
      auto typeOpt = e->getString("type");
      auto nameOpt = e->getString("name");
      if (!typeOpt || !nameOpt)
        return;
      // Skip parameter and return-value entries: no signature rewriting.
      if (*nameOpt == "ret." || e->getBoolean("isParam").value_or(false))
        return;
      if (*typeOpt == parseCIRTypeName(alloca.getAllocaType()))
        return;
      Type newTy = typeNameToCIRType(*typeOpt, ctx);
      if (!newTy)
        return;
      targets.push_back(alloca);
      targetTypes.push_back(newTy);
    });

    for (auto [alloca, newTy] : llvm::zip(targets, targetTypes)) {
      OpBuilder b(alloca);
      auto ptrTy = cir::PointerType::get(newTy);
      auto newAlloca = b.create<cir::AllocaOp>(
          alloca.getLoc(), ptrTy, newTy, alloca.getName(),
          alloca.getAlignmentAttr());
      if (Attribute init = alloca->getAttr("init"))
        newAlloca->setAttr("init", init);
      alloca.getResult().replaceAllUsesWith(newAlloca.getResult());
      alloca.erase();
      changed = true;
      llvm::errs() << "[cir change variable] " << func.getSymName() << "::"
                   << newAlloca.getName() << " -> "
                   << parseCIRTypeName(newTy) << '\n';
    }
    return changed;
  }

  // ---- fixpoint retyping ------------------------------------------------

  // Effective precision constraint of a binop operand, following the LLVM
  // pass rule: fp constants do not lock; pre-existing fp casts are looked
  // through to their source.
  Type effectiveType(Value v) {
    if (auto cst = v.getDefiningOp<cir::ConstantOp>()) {
      if (isCIRFPType(cst.getType()))
        return {};
    }
    if (auto cast = v.getDefiningOp<cir::CastOp>()) {
      if (!insertedCasts.count(cast) &&
          cast.getKind() == cir::CastKind::floating &&
          isCIRFPType(cast.getSrc().getType()))
        return cast.getSrc().getType();
    }
    return v.getType();
  }

  static Type highest(Type a, Type b) {
    if (!a)
      return b;
    if (!b)
      return a;
    return fpTypeWidth(a) >= fpTypeWidth(b) ? a : b;
  }

  void repairFixpoint() {
    erasedOps.clear();
    for (unsigned round = 0; round < 16; ++round) {
      bool changed = false;

      // Snapshot ops; newly created ops are handled in later rounds.
      SmallVector<Operation *> ops;
      func.walk([&](Operation *op) { ops.push_back(op); });

      for (Operation *op : ops) {
        if (erasedOps.contains(op))
          continue; // erased earlier in this round
        if (auto load = dyn_cast<cir::LoadOp>(op))
          changed |= repairLoad(load);
        else if (auto store = dyn_cast<cir::StoreOp>(op))
          changed |= repairStore(store);
        else if (auto binop = dyn_cast<cir::BinOp>(op))
          changed |= repairBinOp(binop);
        else if (auto unop = dyn_cast<cir::UnaryOp>(op))
          changed |= repairUnary(unop);
        else if (auto cast = dyn_cast<cir::CastOp>(op))
          changed |= repairCast(cast);
        else if (auto ret = dyn_cast<cir::ReturnOp>(op))
          changed |= repairReturn(ret);
      }

      if (!changed)
        break;
    }
  }

  bool repairLoad(cir::LoadOp load) {
    auto ptrTy = dyn_cast<cir::PointerType>(load.getAddr().getType());
    if (!ptrTy)
      return false;
    Type pointee = ptrTy.getPointee();
    if (load.getType() == pointee)
      return false;
    if (!isCIRFPType(pointee) && !isCIRFPType(load.getType()))
      return false;
    OpBuilder b(load);
    auto newLoad = b.create<cir::LoadOp>(
        load.getLoc(), pointee, load.getAddr(),
        /*isDeref=*/false, /*isVolatile=*/false, load.getAlignmentAttr(),
        /*sync_scope=*/nullptr, /*mem_order=*/nullptr);
    // Propagate the new type directly; downstream ops (binop, store into
    // another alloca, return ...) are repaired in this or later rounds.
    load.getResult().replaceAllUsesWith(newLoad.getResult());
    erasedOps.insert(load);
    load.erase();
    return true;
  }

  bool repairStore(cir::StoreOp store) {
    auto ptrTy = dyn_cast<cir::PointerType>(store.getAddr().getType());
    if (!ptrTy)
      return false;
    Type pointee = ptrTy.getPointee();
    if (store.getValue().getType() == pointee)
      return false;
    OpBuilder b(store);
    Value nv = castTo(store.getValue(), pointee, b);
    if (!nv)
      return false;
    b.create<cir::StoreOp>(store.getLoc(), nv, store.getAddr(),
                          /*isVolatile=*/false, store.getAlignmentAttr(),
                          /*sync_scope=*/nullptr, /*mem_order=*/nullptr);
    erasedOps.insert(store);
    store.erase();
    return true;
  }

  bool repairBinOp(cir::BinOp binop) {
    if (!isCIRFPType(binop.getType()))
      return false;
    Value lhs = binop.getLhs(), rhs = binop.getRhs();
    Type target = highest(effectiveType(lhs), effectiveType(rhs));
    if (!target)
      return false; // both constants: nothing propagated here
    if (lhs.getType() == target && rhs.getType() == target &&
        binop.getType() == target)
      return false;
    OpBuilder b(binop);
    Value nl = lhs.getType() == target ? lhs : castTo(lhs, target, b);
    Value nr = rhs.getType() == target ? rhs : castTo(rhs, target, b);
    if (!nl || !nr)
      return false;
    auto newOp = b.create<cir::BinOp>(binop.getLoc(), target, binop.getKind(),
                                      nl, nr,
                                      binop.getNoUnsignedWrapAttr() != nullptr,
                                      binop.getNoSignedWrapAttr() != nullptr,
                                      binop.getSaturatedAttr() != nullptr);
    binop.getResult().replaceAllUsesWith(newOp.getResult());
    binop.erase();
    return true;
  }

  bool repairUnary(cir::UnaryOp unop) {
    if (!isCIRFPType(unop.getType()))
      return false;
    Value input = unop.getInput();
    if (input.getType() == unop.getType())
      return false;
    OpBuilder b(unop);
    Value ni = castTo(input, unop.getType(), b);
    if (!ni)
      return false;
    auto newOp = b.create<cir::UnaryOp>(unop.getLoc(), unop.getType(),
                                        unop.getKind(), ni);
    unop.getResult().replaceAllUsesWith(newOp.getResult());
    unop.erase();
    return true;
  }

  bool repairCast(cir::CastOp cast) {
    Value src = cast.getSrc();
    if (src.getType() == cast.getType()) {
      cast.getResult().replaceAllUsesWith(src);
      cast.erase();
      return true;
    }
    return false;
  }

  bool repairReturn(cir::ReturnOp ret) {
    cir::FuncType fty = func.getFunctionType();
    if (fty.getReturnTypes().empty())
      return false;
    Type retTy = fty.getReturnType();
    SmallVector<Value> newArgs;
    bool changed = false;
    OpBuilder b(ret);
    for (Value v : ret.getOperands()) {
      if (v.getType() != retTy) {
        Value nv = castTo(v, retTy, b);
        if (!nv)
          newArgs.push_back(v);
        else {
          newArgs.push_back(nv);
          changed = true;
        }
      } else {
        newArgs.push_back(v);
      }
    }
    if (!changed)
      return false;
    b.create<cir::ReturnOp>(ret.getLoc(), newArgs);
    ret.erase();
    return true;
  }

  void eraseDeadOps() {
    for (unsigned sweep = 0; sweep < 8; ++sweep) {
      bool erased = false;
      SmallVector<Operation *> dead;
      func.walk([&](Operation *op) {
        if (!isa<cir::ConstantOp, cir::CastOp, cir::LoadOp, cir::BinOp,
                 cir::UnaryOp>(op))
          return;
        if (op->use_empty())
          dead.push_back(op);
      });
      for (Operation *op : dead) {
        op->erase();
        erased = true;
      }
      if (!erased)
        break;
    }
  }
};

} // namespace

std::unique_ptr<mlir::Pass> createCIRTransformPass() {
  return std::make_unique<CIRTransform>();
}

void CIRTransform::runOnOperation() {
  ModuleOp module = getOperation();
  llvm::errs() << ">>> Perci-Tuner(CIR): Running CIRTransform Pass...\n";

  TransformConfig varCfg;
  if (!varCfg.load(cirVariableConfig.getValue())) {
    llvm::errs() << "CIRTransform: failed to load variable config: "
                 << cirVariableConfig.getValue() << '\n';
    signalPassFailure();
    return;
  }

  // Phase 1 (M4): math call rewriting.

  // Phase 2: variable precision rewriting.
  module.walk([&](cir::FuncOp func) {
    if (func.isDeclaration())
      return;
    VariableTransformer t(func, varCfg);
    t.run();
  });
}

} // namespace perci
