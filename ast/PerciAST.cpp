#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>
#include <string>

using namespace clang;

namespace {

struct FloatVarInfo {
  std::string File;
  unsigned Line;
  unsigned Column;
  std::string TypeName;
  bool IsGlobal;
};

class PerciASTVisitor : public RecursiveASTVisitor<PerciASTVisitor> {
public:
  explicit PerciASTVisitor(ASTContext *Ctx, std::vector<FloatVarInfo> &Out)
      : Ctx(Ctx), Out(Out) {}

  bool VisitVarDecl(VarDecl *VD) {
    QualType QT = VD->getType();

    // 只关心浮点类型（float / double / long double / __float128 等）
    if (!QT->isFloatingType())
      return true;

    SourceLocation Loc = VD->getLocation();
    if (Loc.isInvalid())
      return true;

    SourceManager &SM = Ctx->getSourceManager();

    // 若只想分析主文件（排除系统头文件），保留下面这行；
    // 若想分析头文件中的变量，请注释掉此行。
    if (!SM.isWrittenInMainFile(Loc))
      return true;

    PresumedLoc PLoc = SM.getPresumedLoc(Loc);
    if (PLoc.isInvalid())
      return true;

    FloatVarInfo Info;
    Info.File = PLoc.getFilename();
    Info.Line = PLoc.getLine();
    Info.Column = PLoc.getColumn();
    Info.TypeName = QT.getAsString();
    // hasGlobalStorage() == true 表示全局/静态变量；false 为局部变量
    Info.IsGlobal = VD->hasGlobalStorage();

    Out.push_back(std::move(Info));
    return true;
  }

private:
  ASTContext *Ctx;
  std::vector<FloatVarInfo> &Out;
};

class PerciASTConsumer : public ASTConsumer {
public:
  explicit PerciASTConsumer(ASTContext *Ctx) : Visitor(Ctx, Infos) {}

  void HandleTranslationUnit(ASTContext &Ctx) override {
    Visitor.TraverseDecl(Ctx.getTranslationUnitDecl());

    llvm::json::Array JArray;
    for (const auto &Info : Infos) {
      llvm::json::Object JObj;
      JObj["file"] = Info.File;
      JObj["line"] = static_cast<int64_t>(Info.Line);
      JObj["column"] = static_cast<int64_t>(Info.Column);
      JObj["type"] = Info.TypeName;
      JObj["is_global"] = Info.IsGlobal;
      JArray.push_back(std::move(JObj));
    }

    llvm::json::Object Root;
    Root["float_variables"] = std::move(JArray);

    std::string JSONStr;
    llvm::raw_string_ostream OS(JSONStr);
    OS << llvm::json::Value(std::move(Root));
    OS.flush();

    // 输出到 stdout（可通过 shell 重定向到文件）
    llvm::outs() << JSONStr << "\n";
  }

private:
  std::vector<FloatVarInfo> Infos;
  PerciASTVisitor Visitor;
};

class PerciASTAction : public PluginASTAction {
public:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef) override {
    return std::make_unique<PerciASTConsumer>(&CI.getASTContext());
  }

  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string> &args) override {
    // 暂不需要额外参数
    return true;
  }
};

} // namespace

static FrontendPluginRegistry::Add<PerciASTAction>
    X("perci-analysis", "Analyze floating-point variable declarations");