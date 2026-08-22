// @Author: Guocheng - NUDT (alpha version)
// Usage: Pure-Clang-AST analyzer for floating-point variables.
// Walks the merged AST and emits config.json with four categories:
//   - local   : { func_name: [ {name,type,line,column,file,isStatic,isParam} ] }
//   - global  : [ {name,isStatic,line,file,type} ]
//   - structs : { struct_name: [ {member_index,type} ] }
// Loaded as a clang frontend plugin:
//   clang -c -Xclang -load -Xclang libPerciAST.so
//         -Xclang -plugin -Xclang perci-analysis
//         -Xclang -ast-merge -Xclang helper.ast ... main.c

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <set>
#include <string>
#include <vector>
#include <string>

using namespace clang;
using namespace llvm;

namespace {

struct LocalVarInfo {
  std::string Name;
  std::string File;
  unsigned Line;
  unsigned Column;
  std::string TypeName;
  bool IsStatic;
  bool IsParam;
};

struct GlobalVarInfo {
  std::string Name;
  std::string File;
  unsigned Line;
  std::string TypeName;
  bool IsStatic;
};

struct StructMemberInfo {
  unsigned MemberIndex;
  std::string TypeName;
};

class PerciASTVisitor
    : public RecursiveASTVisitor<PerciASTVisitor> {
public:
  explicit PerciASTVisitor(
      ASTContext *Ctx,
      std::map<std::string, std::vector<LocalVarInfo>> &Locals,
      std::vector<GlobalVarInfo> &Globals,
      std::map<std::string, std::vector<StructMemberInfo>> &Structs)
      : Ctx(Ctx), Locals(Locals), Globals(Globals), Structs(Structs) {}

  // Handles local + global VarDecls. Function-local vars go into Locals[func];
  // file-scope vars go into Globals. Only definitions are captured, so extern
  // declarations (and re-declarations) are skipped.
  bool VisitVarDecl(VarDecl *VD) {
    // ParmVarDecl is a subclass of VarDecl; VisitParmVarDecl handles params
    // exclusively to avoid double-capture.
    if (isa<ParmVarDecl>(VD))
      return true;

    QualType QT = VD->getType();
    if (!QT->isFloatingType())
      return true;

    if (!VD->isThisDeclarationADefinition())
      return true;

    SourceLocation Loc = VD->getLocation();
    if (Loc.isInvalid())
      return true;

    SourceManager &SM = Ctx->getSourceManager();
    if (SM.isInSystemHeader(Loc))
      return true;

    PresumedLoc PLoc = SM.getPresumedLoc(Loc);
    if (PLoc.isInvalid())
      return true;

    DeclContext *DC = VD->getDeclContext();

    if (isa<TranslationUnitDecl>(DC)) {
      GlobalVarInfo Info;
      Info.Name = VD->getNameAsString();
      Info.File = PLoc.getFilename();
      Info.Line = PLoc.getLine();
      Info.TypeName = QT.getAsString();
      Info.IsStatic = (VD->getStorageClass() == SC_Static);
      Globals.push_back(std::move(Info));
      return true;
    }

    if (auto *FD = dyn_cast<FunctionDecl>(DC)) {
      LocalVarInfo Info;
      Info.Name = VD->getNameAsString();
      Info.File = PLoc.getFilename();
      Info.Line = PLoc.getLine();
      Info.Column = PLoc.getColumn();
      Info.TypeName = QT.getAsString();
      Info.IsStatic = (VD->getStorageClass() == SC_Static);
      Info.IsParam = false;
      Locals[FD->getNameAsString()].push_back(std::move(Info));
    }
    return true;
  }

  // Captures floating-point function parameters. Only visit params of function
  // definitions (not prototypes) to avoid duplicates from header redeclarations.
  bool VisitParmVarDecl(ParmVarDecl *PVD) {
    QualType QT = PVD->getType();
    if (!QT->isFloatingType())
      return true;

    SourceLocation Loc = PVD->getLocation();
    if (Loc.isInvalid())
      return true;

    SourceManager &SM = Ctx->getSourceManager();
    if (SM.isInSystemHeader(Loc))
      return true;

    PresumedLoc PLoc = SM.getPresumedLoc(Loc);
    if (PLoc.isInvalid())
      return true;

    DeclContext *DC = PVD->getDeclContext();
    auto *FD = dyn_cast<FunctionDecl>(DC);
    if (!FD || !FD->isThisDeclarationADefinition())
      return true;

    LocalVarInfo Info;
    Info.Name = PVD->getNameAsString();
    Info.File = PLoc.getFilename();
    Info.Line = PLoc.getLine();
    Info.Column = PLoc.getColumn();
    Info.TypeName = QT.getAsString();
    Info.IsStatic = false;
    Info.IsParam = true;
    Locals[FD->getNameAsString()].push_back(std::move(Info));
    return true;
  }

  // Captures floating-point members of struct/class definitions. Anonymous
  // structs and structs without any floating-point member are skipped.
  bool VisitRecordDecl(RecordDecl *RD) {
    if (!RD->isCompleteDefinition())
      return true;
    if (!RD->isStruct() && !RD->isClass())
      return true;

    SourceLocation Loc = RD->getLocation();
    if (Loc.isInvalid())
      return true;

    SourceManager &SM = Ctx->getSourceManager();
    if (SM.isInSystemHeader(Loc))
      return true;

    std::string structName = RD->getNameAsString();
    if (structName.empty())
      return true;

    if (!ProcessedStructs.insert(structName).second)
      return true;

    std::vector<StructMemberInfo> members;
    unsigned idx = 0;
    for (const FieldDecl *FD : RD->fields()) {
      QualType QT = FD->getType();
      if (!QT->isFloatingType()) {
        ++idx;
        continue;
      }
      StructMemberInfo mi;
      mi.MemberIndex = idx++;
      mi.TypeName = QT.getAsString();
      members.push_back(std::move(mi));
    }
    if (!members.empty())
      Structs[structName] = std::move(members);
    return true;
  }

private:
  ASTContext *Ctx;
  std::map<std::string, std::vector<LocalVarInfo>> &Locals;
  std::vector<GlobalVarInfo> &Globals;
  std::map<std::string, std::vector<StructMemberInfo>> &Structs;
  std::set<std::string> ProcessedStructs;
};

class PerciASTConsumer : public ASTConsumer {
public:
  explicit PerciASTConsumer(ASTContext *Ctx)
      : Visitor(Ctx, Locals, Globals, Structs) {}

  void HandleTranslationUnit(ASTContext &Ctx) override {
    Visitor.TraverseDecl(Ctx.getTranslationUnitDecl());

    json::Object Root;

    // local: { func_name: [ {name,type,line,column,file,isStatic,isParam} ] }
    json::Object LocalObj;
    for (const auto &KV : Locals) {
      json::Array Arr;
      for (const auto &V : KV.second) {
        json::Object O;
        O["name"] = V.Name;
        O["type"] = V.TypeName;
        O["line"] = static_cast<int64_t>(V.Line);
        O["column"] = static_cast<int64_t>(V.Column);
        O["file"] = V.File;
        O["isStatic"] = V.IsStatic;
        O["isParam"] = V.IsParam;
        Arr.push_back(std::move(O));
      }
      LocalObj[KV.first] = std::move(Arr);
    }
    Root["local"] = std::move(LocalObj);

    // global: [ {name,isStatic,line,file,type} ]
    json::Array GlobalArr;
    for (const auto &G : Globals) {
      json::Object O;
      O["name"] = G.Name;
      O["isStatic"] = G.IsStatic;
      O["line"] = static_cast<int64_t>(G.Line);
      O["file"] = G.File;
      O["type"] = G.TypeName;
      GlobalArr.push_back(std::move(O));
    }
    Root["global"] = std::move(GlobalArr);

    // structs: { struct_name: [ {member_index,type} ] }
    json::Object StructsObj;
    for (const auto &KV : Structs) {
      json::Array Arr;
      for (const auto &M : KV.second) {
        json::Object O;
        O["member_index"] = static_cast<int64_t>(M.MemberIndex);
        O["type"] = M.TypeName;
        Arr.push_back(std::move(O));
      }
      StructsObj[KV.first] = std::move(Arr);
    }
    Root["structs"] = std::move(StructsObj);

    std::string JSONStr;
    raw_string_ostream OS(JSONStr);
    OS << formatv("{0:4}", json::Value(std::move(Root)));
    OS.flush();

    outs() << JSONStr << "\n";
  }

private:
  std::map<std::string, std::vector<LocalVarInfo>> Locals;
  std::vector<GlobalVarInfo> Globals;
  std::map<std::string, std::vector<StructMemberInfo>> Structs;
  PerciASTVisitor Visitor;
};

class PerciASTAction : public PluginASTAction {
public:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef) override {
    return std::make_unique<PerciASTConsumer>(&CI.getASTContext());
  }

  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string> &args) override {
    return true;
  }
};

} // namespace

static FrontendPluginRegistry::Add<PerciASTAction> X(
    "perci-analysis",
    "Analyze floating-point variables (local/global/params/structs) from AST");
