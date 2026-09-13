//===- CIRConfig.h - Shared helpers for CIR perci passes -------*- C++ -*-===//
//
// Shared (header-only) utilities for CIRPrintConfig and CIRTransform:
// command-line options, config.json key rules, math function set and
// type-name <-> CIR type mapping. Semantics mirror the LLVM IR passes in
// lib/ (PrintConfig.cpp / ChangeVariableDecl.cpp / ChangeMathCall.cpp).
//
//===----------------------------------------------------------------------===//

#ifndef PERCI_CIR_CONFIG_H
#define PERCI_CIR_CONFIG_H

#include "clang/CIR/Dialect/IR/CIRDialect.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Path.h"

#include <map>
#include <string>
#include <vector>

namespace perci {

namespace json = llvm::json;

// ---------------------------------------------------------------------------
// Command-line options (inline to share one definition across both passes)
// ---------------------------------------------------------------------------

inline llvm::cl::opt<std::string>
    cirVariableConfig("variable-config", llvm::cl::value_desc("filename"),
                      llvm::cl::desc("Variable's Type Config"),
                      llvm::cl::init("config.json"));

inline llvm::cl::opt<std::string>
    cirFunctionConfig("function-config", llvm::cl::value_desc("filename"),
                      llvm::cl::desc("Function/math-call's Type Config"),
                      llvm::cl::init("config.json"));

inline llvm::cl::opt<bool>
    cirEnableMXP("mxp", llvm::cl::value_desc("enable pragma & autofp"),
                 llvm::cl::desc("true or false"), llvm::cl::init(false));

inline llvm::cl::opt<std::string>
    cirExcludeFiles("exclude-file", llvm::cl::value_desc("files skipped"),
                    llvm::cl::desc("comma separated src files"),
                    llvm::cl::init(""));
inline llvm::cl::opt<std::string>
    cirExcludeFuncs("exclude-func", llvm::cl::value_desc("functions skipped"),
                    llvm::cl::desc("comma separated function names"),
                    llvm::cl::init(""));
inline llvm::cl::opt<std::string>
    cirIncludeFiles("include-file", llvm::cl::value_desc("files included"),
                    llvm::cl::desc("comma separated src files"),
                    llvm::cl::init(""));
inline llvm::cl::opt<std::string>
    cirIncludeFuncs("include-func", llvm::cl::value_desc("functions included"),
                    llvm::cl::desc("comma separated function names"),
                    llvm::cl::init(""));

// Same set as lib/ChangeMathCall.h (precision-suffixed C math functions).
inline const llvm::DenseSet<llvm::StringRef> &mathFunctions() {
  static const llvm::DenseSet<llvm::StringRef> kMath = {
      // 三角函数
      "sin", "sinf", "sinl", "cos", "cosf", "cosl", "tan", "tanf", "tanl",
      "asin", "asinf", "asinl", "acos", "acosf", "acosl", "atan", "atanf",
      "atanl",
      // 双曲函数
      "sinh", "sinhf", "sinhl", "cosh", "coshf", "coshl", "tanh", "tanhf",
      "tanhl",
      // 指数对数
      "exp", "expf", "expl", "log", "logf", "logl",
      "exp2", "exp2f", "exp2l", "log2", "log2f", "log2l",
      "log10", "log10f", "log10l",
      // 幂和根
      "sqrt", "sqrtf", "sqrtl", "cbrt", "cbrtf", "cbrtl",
      "pow", "powf", "powl", "hypot", "hypotf", "hypotl",
      // 其他
      "fabs", "fabsf", "fabsl",
      "round", "roundf", "roundl",
      "fmod", "fmodf", "fmodl",
      "atan2", "atan2f", "atan2l",
      "erf", "erff", "erfl",
      "tgamma", "tgammaf", "tgammal"};
  return kMath;
}

// Strip the precision suffix of a math function name: sinf/sinl -> sin.
inline llvm::StringRef mathBaseName(llvm::StringRef name) {
  if (!name.empty() && (name.back() == 'f' || name.back() == 'l'))
    return name.drop_back();
  return name;
}

// Precision type for a target math callee: 'f' -> float, 'l' -> long double,
// otherwise double. Mirrors lib/ChangeMathCall.cpp parameter type decision.
enum class FPKind { FP16, Float, Double, LongDouble };

// Build the file:line:column config key, identical to the LLVM IR passes.
inline std::string makeKey(llvm::StringRef file, unsigned line,
                           unsigned column) {
  return (file + ":" + llvm::Twine(line) + ":" + llvm::Twine(column)).str();
}

// ---------------------------------------------------------------------------
// Include / exclude file & function filters (comma separated globs)
// ---------------------------------------------------------------------------

inline std::vector<std::string> parseCommaList(llvm::StringRef raw) {
  std::vector<std::string> items;
  size_t pos = 0;
  while (pos < raw.size()) {
    size_t comma = raw.find(',', pos);
    llvm::StringRef token =
        comma == llvm::StringRef::npos ? raw.substr(pos) : raw.substr(pos, comma - pos);
    token = token.trim();
    if (!token.empty())
      items.push_back(token.str());
    if (comma == llvm::StringRef::npos)
      break;
    pos = comma + 1;
  }
  return items;
}

struct ScanFilters {
  std::vector<std::string> excludeFiles, excludeFuncs;
  std::vector<std::string> includeFiles, includeFuncs;

  static bool globMatch(llvm::StringRef pattern, llvm::StringRef value) {
    size_t p = 0, v = 0;
    size_t star = llvm::StringRef::npos, starValue = 0;
    while (v < value.size()) {
      if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == value[v])) {
        ++p;
        ++v;
      } else if (p < pattern.size() && pattern[p] == '*') {
        star = p++;
        starValue = v;
      } else if (star != llvm::StringRef::npos) {
        p = star + 1;
        v = ++starValue;
      } else {
        return false;
      }
    }
    while (p < pattern.size() && pattern[p] == '*')
      ++p;
    return p == pattern.size();
  }

  static bool matchPattern(llvm::StringRef pattern, llvm::StringRef value) {
    if (pattern.empty())
      return false;
    if (pattern.contains('*') || pattern.contains('?'))
      return globMatch(pattern, value);
    return value == pattern || value.contains(pattern) ||
           pattern.contains(value);
  }

  bool matchAny(const std::vector<std::string> &patterns,
                llvm::ArrayRef<llvm::StringRef> values) const {
    for (const std::string &pat : patterns)
      for (llvm::StringRef v : values)
        if (matchPattern(pat, v))
          return true;
    return false;
  }

  bool needScanFile(llvm::StringRef file) const {
    llvm::StringRef base = llvm::sys::path::filename(file);
    return (includeFiles.empty() ||
            matchAny(includeFiles, {base, file})) &&
           !matchAny(excludeFiles, {base, file});
  }

  bool needScanFunction(llvm::StringRef name) const {
    std::string demangled = llvm::demangle(name.str());
    return (includeFuncs.empty() ||
            matchAny(includeFuncs, {demangled, name})) &&
           !matchAny(excludeFuncs, {demangled, name});
  }
};

inline ScanFilters getScanFilters() {
  ScanFilters f;
  f.excludeFiles = parseCommaList(cirExcludeFiles.getValue());
  f.excludeFuncs = parseCommaList(cirExcludeFuncs.getValue());
  f.includeFiles = parseCommaList(cirIncludeFiles.getValue());
  f.includeFuncs = parseCommaList(cirIncludeFuncs.getValue());
  return f;
}

// ---------------------------------------------------------------------------
// Source locations
// ---------------------------------------------------------------------------

struct SrcLoc {
  std::string file; // basename, mirroring DIFile filename
  unsigned line = 0;
  unsigned column = 0;
};

// CIR attaches fused declaration ranges to allocas (e.g.
// fused[start, end]); the last FileLineColLoc distinguishes multiple
// declarators in one declaration, so it is used as the variable location.
inline SrcLoc getSrcLoc(mlir::Location loc) {
  SrcLoc result;
  mlir::Location cur = loc;
  for (;;) {
    if (auto flc = mlir::dyn_cast<mlir::FileLineColLoc>(cur)) {
      result.file = llvm::sys::path::filename(flc.getFilename()).str();
      result.line = flc.getLine();
      result.column = flc.getColumn();
      return result;
    }
    if (auto fused = mlir::dyn_cast<mlir::FusedLoc>(cur)) {
      SrcLoc sub;
      for (mlir::Location l : fused.getLocations())
        if (SrcLoc s = getSrcLoc(l); !s.file.empty())
          sub = s; // keep the last available file location
      return sub;
    }
    if (auto callSite = mlir::dyn_cast<mlir::CallSiteLoc>(cur)) {
      cur = callSite.getCallee();
      continue;
    }
    if (auto nameLoc = mlir::dyn_cast<mlir::NameLoc>(cur)) {
      cur = nameLoc.getChildLoc();
      continue;
    }
    return result;
  }
}

// ---------------------------------------------------------------------------
// CIR type <-> config type-name strings
// (mirrors lib/utils/ParseTypeName.hpp for the types CIR can express)
// ---------------------------------------------------------------------------

inline std::string parseCIRTypeName(mlir::Type type);

inline std::string parseCIRTypeNameImpl(mlir::Type type) {
  if (!type)
    return "";

  // CIR floating-point types
  if (mlir::isa<cir::FP16Type>(type))
    return "__fp16";
  if (mlir::isa<cir::SingleType>(type))
    return "float";
  if (mlir::isa<cir::DoubleType>(type))
    return "double";
  if (mlir::isa<cir::LongDoubleType, cir::FP128Type, cir::FP80Type>(type))
    return "long double";
  if (mlir::isa<cir::BF16Type>(type))
    return "__bf16";

  // Builtin MLIR floats (defensive: CIR normally uses the CIR types above)
  if (auto ft = mlir::dyn_cast<mlir::FloatType>(type)) {
    if (ft.isF16())
      return "__fp16";
    if (ft.isBF16())
      return "__bf16";
    if (ft.isF32())
      return "float";
    if (ft.isF64())
      return "double";
    return "long double";
  }

  if (mlir::isa<cir::IntType, cir::BoolType, mlir::IntegerType>(type))
    return "integer";

  if (auto rec = mlir::dyn_cast<cir::RecordType>(type))
    return rec.getName() ? rec.getName().str() : "";

  if (auto arr = mlir::dyn_cast<cir::ArrayType>(type))
    return parseCIRTypeName(arr.getElementType()) +
           "[" + std::to_string(arr.getSize()) + "]";

  if (auto ptr = mlir::dyn_cast<cir::PointerType>(type))
    return parseCIRTypeName(ptr.getPointee()) + "*";

  // Fallback: use the printed type.
  std::string out;
  llvm::raw_string_ostream os(out);
  type.print(os);
  os.flush();
  return out;
}

inline std::string parseCIRTypeName(mlir::Type type) {
  std::string name = parseCIRTypeNameImpl(type);
  if (name == "__autofp")
    name = "double"; // default (specified by clang)
  return name;
}

// True when the config type-name refers to a floating-point scalar /
// array / pointer-to float (matches the LLVM pass's typeNameToBasicType
// acceptance rule).
inline bool isFloatingTypeName(llvm::StringRef name) {
  return name.consume_back("*") || name.contains("__fp16") ||
         name.contains("float") || name.contains("double") ||
         name.contains("long double");
}

// ---------------------------------------------------------------------------
// Type predicates and config type-name -> CIR type
// ---------------------------------------------------------------------------

inline bool isCIRFPType(mlir::Type type) {
  if (mlir::isa<cir::FP16Type, cir::BF16Type, cir::SingleType, cir::DoubleType,
                cir::LongDoubleType, cir::FP128Type, cir::FP80Type>(type))
    return true;
  return mlir::isa<mlir::FloatType>(type);
}

inline bool isCIRIntType(mlir::Type type) {
  return mlir::isa<cir::IntType, cir::BoolType, mlir::IntegerType>(type);
}

// Floating-point width in bits (for highest-precision inference).
inline unsigned fpTypeWidth(mlir::Type type) {
  if (auto fpIface = mlir::dyn_cast<cir::FPTypeInterface>(type))
    return fpIface.getWidth();
  if (auto ft = mlir::dyn_cast<mlir::FloatType>(type))
    return ft.getWidth();
  return 0;
}

// Scalar CIR floating-point type for a config type-name. Returns a null Type
// when the name does not denote a supported scalar float. Array suffixes are
// handled by typeNameToCIRType.
inline mlir::Type scalarFPTypeFromName(llvm::StringRef name,
                                       mlir::MLIRContext *ctx) {
  if (name.contains("__fp16"))
    return cir::FP16Type::get(ctx);
  if (name.contains("__bf16"))
    return cir::BF16Type::get(ctx);
  if (name.contains("long double"))
    return cir::LongDoubleType::get(ctx, cir::FP128Type::get(ctx));
  if (name.contains("float"))
    return cir::SingleType::get(ctx);
  if (name.contains("double"))
    return cir::DoubleType::get(ctx);
  return {};
}

// Config type-name -> CIR type, including the "T[N]" array suffix and
// trailing "*" pointer suffix. Returns null for unsupported names.
inline mlir::Type typeNameToCIRType(llvm::StringRef name,
                                    mlir::MLIRContext *ctx) {
  llvm::StringRef core = name;
  // Array suffix T[N]
  if (size_t lb = core.find('['); lb != llvm::StringRef::npos) {
    size_t rb = core.find(']', lb);
    if (rb == llvm::StringRef::npos)
      return {};
    unsigned size = 0;
    if (core.slice(lb + 1, rb - lb - 1).getAsInteger(10, size))
      return {};
    mlir::Type elem = typeNameToCIRType(core.slice(0, lb), ctx);
    if (!elem)
      return {};
    return cir::ArrayType::get(ctx, elem, size);
  }
  unsigned ptrDepth = 0;
  while (core.consume_back("*"))
    ++ptrDepth;
  mlir::Type base = scalarFPTypeFromName(core, ctx);
  if (!base && core.contains("integer"))
    return {}; // integer conversions are out of scope
  if (!base)
    return {};
  while (ptrDepth--)
    base = cir::PointerType::get(base);
  return base;
}

} // namespace perci

#endif // PERCI_CIR_CONFIG_H
