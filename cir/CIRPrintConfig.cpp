//===- CIRPrintConfig.cpp - Print floating-point config from CIR ---------===//
//
// CIR counterpart of lib/PrintConfig.cpp. Emits config.json with the same
// schema:
//   "calls":  { function: [ {type, file, line, column}, ... ] }
//   "global": [ {name, type, file, line, isStatic}, ... ]
//   "local":  { function: [ {name, type, file, line, column,
//                            isStatic, isParam, range}, ... ] }
//   "structs":{ RecordName: [ {member_index, type}, ... ] }
//
// Variable locations come from cir.alloca name + loc (no debug info needed);
// function-local statics are represented by "private" cir.global ops named
// "func.var".
//
//===----------------------------------------------------------------------===//

#include "CIRPrintConfig.h"
#include "CIRConfig.hpp"

#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

#include "mlir/IR/SymbolTable.h"

#include <algorithm>

using namespace mlir;

namespace perci {

namespace {

class ConfigEmitter {
public:
  explicit ConfigEmitter(ModuleOp module, const ScanFilters &filters)
      : module(module), filters(filters) {}

  json::Object run() {
    json::Object root;
    json::Object locals;
    json::Object calls;
    json::Object structs;

    scanGlobals(locals, structs);
    scanFunctions(locals, calls, structs);

    root["calls"] = json::Value(std::move(calls));
    root["global"] = json::Value(std::move(globals));
    root["local"] = json::Value(std::move(locals));
    // "structs" is created on demand in the LLVM pass (dumpStructDecl).
    if (!structs.empty())
      root["structs"] = json::Value(std::move(structs));
    return root;
  }

private:
  ModuleOp module;
  const ScanFilters &filters;
  json::Array globals;

  // Record names already dumped into "structs" (StringAttr data is owned
  // by the MLIR context, so the StringRefs stay valid).
  llvm::DenseSet<llvm::StringRef> dumpedRecords;

  json::Object makeVarEntry(llvm::StringRef name, llvm::StringRef type,
                            const SrcLoc &loc, bool isStatic, bool isParam) {
    json::Object e;
    e["name"] = json::Value(name.str());
    e["type"] = json::Value(type.str());
    e["line"] = json::Value(static_cast<int64_t>(loc.line));
    e["column"] = json::Value(static_cast<int64_t>(loc.column));
    e["file"] = json::Value(loc.file);
    e["isStatic"] = json::Value(isStatic);
    e["isParam"] = json::Value(isParam);
    json::Array range; // MXP not supported in the CIR version yet
    e["range"] = json::Value(std::move(range));
    return e;
  }

  void dumpRecord(cir::RecordType rec, json::Object &structs) {
    llvm::StringRef recName = rec.getName() ? rec.getName().getValue() : "";
    if (recName.empty() || !dumpedRecords.insert(recName).second)
      return;
    json::Array members;
    unsigned idx = 0;
    for (Type memberTy : rec.getMembers()) {
      json::Object m;
      m["member_index"] = json::Value(static_cast<int64_t>(idx++));
      m["type"] = json::Value(parseCIRTypeName(memberTy));
      members.push_back(json::Value(std::move(m)));
    }
    structs[recName.str()] = json::Value(std::move(members));
  }

  // Collect record types reachable from a variable/global declared type
  // (unwrapping arrays, but not pointers).
  void collectRecords(Type type, json::Object &structs) {
    while (auto arr = dyn_cast<cir::ArrayType>(type))
      type = arr.getElementType();
    if (auto rec = dyn_cast<cir::RecordType>(type))
      dumpRecord(rec, structs);
  }

  void scanGlobals(json::Object &locals, json::Object &structs) {
    // Function-local statics grouped by owning function; sorted later.
    std::map<std::string, json::Array> localStatics;

    for (cir::GlobalOp global : module.getOps<cir::GlobalOp>()) {
      if (global.isDeclaration())
        continue;
      llvm::StringRef symName = global.getSymName();
      if (symName.contains('.')) {
        // Function-local static: symbol is "func.var".
        size_t dot = symName.find('.');
        std::string funcName = symName.substr(0, dot).str();
        llvm::StringRef varName = symName.substr(dot + 1);
        SrcLoc loc = getSrcLoc(global.getLoc());
        json::Object e =
            makeVarEntry(varName, parseCIRTypeName(global.getSymType()), loc,
                         /*isStatic=*/true, /*isParam=*/false);
        localStatics[funcName].push_back(json::Value(std::move(e)));
        collectRecords(global.getSymType(), structs);
        continue;
      }

      SrcLoc loc = getSrcLoc(global.getLoc());
      if (!filters.needScanFile(loc.file))
        continue;

      bool isStatic =
          SymbolTable::getSymbolVisibility(global) ==
          SymbolTable::Visibility::Private;
      json::Object e;
      e["name"] = json::Value(symName.str());
      e["type"] = json::Value(parseCIRTypeName(global.getSymType()));
      e["file"] = json::Value(loc.file);
      e["line"] = json::Value(static_cast<int64_t>(loc.line));
      e["isStatic"] = json::Value(isStatic);
      globals.push_back(json::Value(std::move(e)));
      collectRecords(global.getSymType(), structs);
    }

    for (auto &[funcName, arr] : localStatics)
      pendingLocalStatics[funcName] = std::move(arr);
  }

  // Function-local statics collected while scanning globals.
  std::map<std::string, json::Array> pendingLocalStatics;

  // Locate the entry alloca each function argument is stored into.
  llvm::DenseSet<Operation *> findParamAllocas(cir::FuncOp func) {
    llvm::DenseSet<Operation *> result;
    func.walk([&](cir::StoreOp store) {
      Value val = store.getValue();
      if (isa<BlockArgument>(val))
        if (auto alloca = store.getAddr().getDefiningOp<cir::AllocaOp>())
          result.insert(alloca);
    });
    return result;
  }

  void scanFunctions(json::Object &locals, json::Object &calls,
                     json::Object &structs) {
    for (cir::FuncOp func : module.getOps<cir::FuncOp>()) {
      if (func.isDeclaration())
        continue;
      std::string funcName = llvm::demangle(func.getSymName().str());
      if (!filters.needScanFunction(funcName))
        continue;

      SrcLoc funcLoc = getSrcLoc(func.getLoc());
      if (!filters.needScanFile(funcLoc.file))
        continue;

      llvm::DenseSet<Operation *> paramAllocas = findParamAllocas(func);

      json::Array vars;
      func.walk([&](cir::AllocaOp alloca) {
        llvm::StringRef varName = alloca.getName();
        if (varName.empty() || varName == "__retval")
          return;
        Type allocated = alloca.getAllocaType();
        std::string typeName = parseCIRTypeName(allocated);
        if (typeName.find("integer") != std::string::npos)
          return; // mirror PrintConfig's integer filter
        bool isParam = paramAllocas.count(alloca.getOperation()) &&
                       !isa<cir::PointerType>(allocated);
        SrcLoc loc = getSrcLoc(alloca.getLoc());
        vars.push_back(json::Value(
            makeVarEntry(varName, typeName, loc, /*isStatic=*/false, isParam)));
        collectRecords(allocated, structs);
      });

      // Function-local static variables.
      if (auto it = pendingLocalStatics.find(func.getSymName().str());
          it != pendingLocalStatics.end()) {
        for (json::Value &v : it->second)
          vars.push_back(std::move(v));
        it->second = json::Array();
      }

      // Merge all entries in source order, ret. appended last.
      std::stable_sort(vars.begin(), vars.end(),
                       [](const json::Value &a, const json::Value &b) {
                         auto *ao = a.getAsObject();
                         auto *bo = b.getAsObject();
                         int la = (int)*ao->get("line")->getAsInteger();
                         int lb = (int)*bo->get("line")->getAsInteger();
                         if (la != lb)
                           return la < lb;
                         int ca = (int)*ao->get("column")->getAsInteger();
                         int cb = (int)*bo->get("column")->getAsInteger();
                         return ca < cb;
                       });

      // Return value ("ret."): mirror lib/PrintConfig.cpp, skipped for
      // integer return types.
      cir::FuncType fty = func.getFunctionType();
      if (fty.getReturnTypes().size() == 1) {
        std::string retType = parseCIRTypeName(fty.getReturnType());
        if (retType.find("integer") == std::string::npos) {
          json::Object e;
          e["name"] = json::Value("ret.");
          e["type"] = json::Value(std::move(retType));
          e["line"] = json::Value(int64_t{0});
          e["column"] = json::Value(int64_t{0});
          e["file"] = json::Value(funcLoc.file);
          e["isStatic"] = json::Value(false);
          e["isParam"] = json::Value(false);
          e["range"] = json::Value(json::Array());
          vars.push_back(json::Value(std::move(e)));
        }
      }

      locals[funcName] = json::Value(std::move(vars));

      // Calls: every direct, named call in the function (the LLVM pass does
      // not restrict this to math functions; math filtering happens later in
      // ChangeMathCall).
      json::Array callArr;
      func.walk([&](cir::CallOp call) {
        std::optional<llvm::StringRef> callee = call.getCallee();
        if (!callee)
          return;
        SrcLoc loc = getSrcLoc(call.getLoc());
        if (!filters.needScanFile(loc.file))
          return;
        json::Object e;
        e["type"] = json::Value(callee->str());
        e["file"] = json::Value(loc.file);
        e["line"] = json::Value(static_cast<int64_t>(loc.line));
        e["column"] = json::Value(static_cast<int64_t>(loc.column));
        callArr.push_back(json::Value(std::move(e)));
      });
      calls[funcName] = json::Value(std::move(callArr));
    }
  }
};

} // namespace

std::unique_ptr<mlir::Pass> createCIRPrintConfigPass() {
  return std::make_unique<CIRPrintConfig>();
}

void CIRPrintConfig::runOnOperation() {
  ModuleOp module = getOperation();
  llvm::errs() << ">>> Perci-Tuner(CIR): Running CIRPrintConfig Pass...\n";

  ScanFilters filters = getScanFilters();
  json::Object root = ConfigEmitter(module, filters).run();

  std::error_code ec;
  llvm::raw_fd_ostream out("config.json", ec);
  if (ec) {
    llvm::errs() << "CIRPrintConfig: failed to open config.json: "
                 << ec.message() << '\n';
    signalPassFailure();
    return;
  }
  out << llvm::formatv("{0:4}", json::Value(std::move(root)));
  out.flush();
}

} // namespace perci
