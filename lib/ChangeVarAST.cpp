// @Author: Guocheng - NUDT (alpha version)
// Usage: AST-level mixed-precision transformation via source rewriting.
// Reads a config.json (same format as PrintConfig/PerciAST output) and
// rewrites declaration type tokens in the source text, emitting *.changed.c
// per modified file. Clang's Sema/CodeGen handles implicit casts on recompile,
// so no manual IR repair is needed (unlike lib/ChangeVariableDecl.cpp).
//
// Matching (mirrors IR ChangeVariableDecl):
//   - locals/params : by "file:line:column"
//   - globals       : by name (definitions only)
//   - struct members: by struct name + member_index
//
// Multi-declaration splitting: `float a, b;` where a->double, b->float becomes
// `double a; float b;`.
//
// Loaded as a clang frontend plugin:
//   clang -c -Xclang -load -Xclang libChangeVarAST.so
//         -Xclang -plugin -Xclang change-var-ast
//         [-Xclang -plugin-arg-change-var-ast -Xclang <config.json>]
//         source.c

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace clang;

namespace {

class ChangeVarASTVisitor
    : public RecursiveASTVisitor<ChangeVarASTVisitor> {
public:
  explicit ChangeVarASTVisitor(
      ASTContext *Ctx, Rewriter &R,
      const std::map<std::string, std::string> &LocalMap,
      const std::map<std::string, std::string> &GlobalMap,
      const std::map<std::string, std::map<unsigned, std::string>> &StructMap,
      std::set<FileID> &ModifiedFiles)
      : Ctx(Ctx), R(R), LocalMap(LocalMap), GlobalMap(GlobalMap),
        StructMap(StructMap), ModifiedFiles(ModifiedFiles) {}

  // VarDecl covers globals + params. Function-local non-params are handled by
  // VisitDeclStmt (which also does declaration splitting), so skip them here to
  // avoid double-rewriting.
  bool VisitVarDecl(VarDecl *VD) {
    if (ProcessedDecls.count(VD))
      return true;

    if (isa<ParmVarDecl>(VD))
      return handleParam(cast<ParmVarDecl>(VD));

    if (isa<FunctionDecl>(VD->getDeclContext()))
      return true;  // local non-param: VisitDeclStmt owns it

    if (isa<TranslationUnitDecl>(VD->getDeclContext()))
      handleGlobal(VD);

    return true;
  }

  // Grouped local declarations (single or multi). Handles the split case
  // where vars in the same decl-group have different target types.
  bool VisitDeclStmt(DeclStmt *DS) {
    DeclGroupRef DG = DS->isSingleDecl()
                          ? DeclGroupRef(DS->getSingleDecl())
                          : DS->getDeclGroup();

    std::vector<VarDecl *> decls;
    for (auto *D : DG)
      if (auto *VD = dyn_cast<VarDecl>(D))
        decls.push_back(VD);
    if (decls.empty())
      return true;

    for (auto *VD : decls)
      ProcessedDecls.insert(VD);

    std::vector<std::string> targets;
    for (auto *VD : decls) {
      std::string t = getTargetType(VD);
      if (t.empty())
        t = VD->getType().getAsString();
      targets.push_back(t);
    }

    bool allSame = true;
    for (size_t i = 1; i < targets.size(); ++i) {
      if (targets[i] != targets[0]) {
        allSame = false;
        break;
      }
    }

    if (allSame) {
      if (targets[0] == decls[0]->getType().getAsString())
        return true;
      rewriteBuiltinTypeToken(decls[0], targets[0]);
      return true;
    }

    splitDeclGroup(DS, decls, targets);
    return true;
  }

  bool VisitFieldDecl(FieldDecl *FD) {
    RecordDecl *RD = FD->getParent();
    if (!RD || !RD->isCompleteDefinition())
      return true;

    std::string structName = RD->getNameAsString();
    if (structName.empty())
      return true;

    auto sit = StructMap.find(structName);
    if (sit == StructMap.end())
      return true;

    unsigned idx = 0;
    for (const FieldDecl *F : RD->fields()) {
      if (F == FD)
        break;
      ++idx;
    }

    auto mit = sit->second.find(idx);
    if (mit == sit->second.end())
      return true;
    if (mit->second == FD->getType().getAsString())
      return true;

    rewriteBuiltinTypeToken(FD, mit->second);
    return true;
  }

private:
  // Returns the target type string from config, or "" if no match.
  std::string getTargetType(VarDecl *VD) {
    SourceManager &SM = Ctx->getSourceManager();
    SourceLocation Loc = VD->getLocation();
    if (Loc.isInvalid() || SM.isInSystemHeader(Loc))
      return "";

    PresumedLoc PLoc = SM.getPresumedLoc(Loc);
    if (PLoc.isInvalid())
      return "";

    std::string key = std::string(PLoc.getFilename()) + ":" +
                      std::to_string(PLoc.getLine()) + ":" +
                      std::to_string(PLoc.getColumn());
    auto it = LocalMap.find(key);
    if (it != LocalMap.end())
      return it->second;

    if (isa<TranslationUnitDecl>(VD->getDeclContext()) &&
        VD->isThisDeclarationADefinition()) {
      auto git = GlobalMap.find(VD->getNameAsString());
      if (git != GlobalMap.end())
        return git->second;
    }
    return "";
  }

  void handleGlobal(VarDecl *VD) {
    ProcessedDecls.insert(VD);
    std::string target = getTargetType(VD);
    if (target.empty() || target == VD->getType().getAsString())
      return;
    rewriteBuiltinTypeToken(VD, target);
  }

  bool handleParam(ParmVarDecl *PVD) {
    ProcessedDecls.insert(PVD);
    const auto *FD = dyn_cast<FunctionDecl>(PVD->getDeclContext());
    if (!FD || !FD->isThisDeclarationADefinition())
      return true;

    std::string target = getTargetType(PVD);
    if (target.empty() || target == PVD->getType().getAsString())
      return true;
    rewriteBuiltinTypeToken(PVD, target);
    return true;
  }

  // Replace the innermost builtin-type keyword (e.g. "float" in "float *p" or
  // "const float x") so pointer/qualifier structure is preserved.
  void rewriteBuiltinTypeToken(DeclaratorDecl *DD, const std::string &target) {
    TypeSourceInfo *TSI = DD->getTypeSourceInfo();
    if (!TSI)
      return;
    TypeLoc TL = TSI->getTypeLoc();
    while (true) {
      TypeLoc next = TL.getNextTypeLoc();
      if (next.isNull())
        break;
      TL = next;
    }
    SourceRange range = TL.getSourceRange();
    if (range.isValid()) {
      recordModified(range.getBegin());
      R.ReplaceText(range, target);
    }
  }

  // Rewrite an entire multi-var DeclStmt whose members need different types.
  // Groups consecutive vars with the same target type, preserving initializers.
  void splitDeclGroup(DeclStmt *DS, const std::vector<VarDecl *> &decls,
                      const std::vector<std::string> &targets) {
    std::string newText;
    std::string prevType;
    for (size_t i = 0; i < decls.size(); ++i) {
      if (targets[i] != prevType) {
        if (!newText.empty())
          newText += "; ";
        newText += targets[i] + " ";
        prevType = targets[i];
      } else {
        newText += ", ";
      }
      newText += decls[i]->getNameAsString();
      if (decls[i]->hasInit()) {
        SourceRange initRange = decls[i]->getInit()->getSourceRange();
        std::string initText = R.getRewrittenText(initRange);
        newText += " = " + initText;
      }
    }
    newText += ";";

    SourceRange stmtRange = DS->getSourceRange();
    recordModified(stmtRange.getBegin());
    R.ReplaceText(stmtRange, newText);
  }

  void recordModified(SourceLocation Loc) {
    ModifiedFiles.insert(Ctx->getSourceManager().getFileID(Loc));
  }

  ASTContext *Ctx;
  Rewriter &R;
  const std::map<std::string, std::string> &LocalMap;
  const std::map<std::string, std::string> &GlobalMap;
  const std::map<std::string, std::map<unsigned, std::string>> &StructMap;
  std::set<FileID> &ModifiedFiles;
  std::set<VarDecl *> ProcessedDecls;
};

class ChangeVarASTConsumer : public ASTConsumer {
public:
  explicit ChangeVarASTConsumer(ASTContext *Ctx, std::string ConfigPath)
      : Ctx(Ctx), ConfigPath(std::move(ConfigPath)) {}

  void HandleTranslationUnit(ASTContext &Context) override {
    if (!parseConfig())
      return;

    Rewriter R;
    R.setSourceMgr(Context.getSourceManager(), Context.getLangOpts());

    ChangeVarASTVisitor Visitor(Ctx, R, LocalMap, GlobalMap, StructMap,
                                ModifiedFiles);
    Visitor.TraverseDecl(Context.getTranslationUnitDecl());

    SourceManager &SM = Context.getSourceManager();
    for (const FileID &fid : ModifiedFiles) {
      const llvm::RewriteBuffer *rb = R.getRewriteBufferFor(fid);
      if (!rb)
        continue;
      if (SM.isInSystemHeader(SM.getLocForStartOfFile(fid)))
        continue;

      auto FE = SM.getFileEntryRefForID(fid);
      if (!FE)
        continue;
      std::string filename = FE->getName().str();

      std::string outName = makeOutputName(filename);
      std::ofstream out(outName, std::ios::binary);
      if (!out.is_open()) {
        llvm::errs() << "[ChangeVarAST] Cannot write: " << outName << "\n";
        continue;
      }
      std::string content(rb->begin(), rb->end());
      out << content;
      out.close();
      llvm::outs() << "[ChangeVarAST] Written: " << outName << "\n";
    }
  }

private:
  bool parseConfig() {
    std::ifstream f(ConfigPath);
    if (!f.is_open()) {
      llvm::errs() << "[ChangeVarAST] Cannot open config: " << ConfigPath
                   << "\n";
      return false;
    }
    ConfigContents.assign(std::istreambuf_iterator<char>(f),
                          std::istreambuf_iterator<char>());

    auto parsed = llvm::json::parse(llvm::StringRef(ConfigContents));
    if (!parsed) {
      llvm::errs() << "[ChangeVarAST] Failed to parse config JSON\n";
      return false;
    }
    llvm::json::Value config = std::move(*parsed);
    const auto *root = config.getAsObject();
    if (!root)
      return false;

    if (const auto *localVal = root->get("local")) {
      if (const auto *localObj = localVal->getAsObject()) {
        for (const auto &kv : *localObj) {
          if (const auto *arr = kv.second.getAsArray()) {
            for (const auto &entry : *arr) {
              const auto *obj = entry.getAsObject();
              if (!obj)
                continue;
              const auto fileOpt = obj->getString("file");
              const auto lineOpt = obj->getNumber("line");
              const auto colOpt = obj->getNumber("column");
              const auto typeOpt = obj->getString("type");
              if (!fileOpt || !lineOpt || !colOpt || !typeOpt)
                continue;
              std::string key = fileOpt->str() + ":" +
                                 std::to_string((unsigned)*lineOpt) + ":" +
                                 std::to_string((unsigned)*colOpt);
              LocalMap[key] = typeOpt->str();
            }
          }
        }
      }
    }

    if (const auto *globalVal = root->get("global")) {
      if (const auto *globalArr = globalVal->getAsArray()) {
        for (const auto &entry : *globalArr) {
          const auto *obj = entry.getAsObject();
          if (!obj)
            continue;
          const auto nameOpt = obj->getString("name");
          const auto typeOpt = obj->getString("type");
          if (!nameOpt || !typeOpt)
            continue;
          GlobalMap[nameOpt->str()] = typeOpt->str();
        }
      }
    }

    if (const auto *structsVal = root->get("structs")) {
      if (const auto *structsObj = structsVal->getAsObject()) {
        for (const auto &kv : *structsObj) {
          std::string structName = kv.first.str();
          if (const auto *arr = kv.second.getAsArray()) {
            std::map<unsigned, std::string> members;
            for (const auto &entry : *arr) {
              const auto *obj = entry.getAsObject();
              if (!obj)
                continue;
              const auto idxOpt = obj->getNumber("member_index");
              const auto typeOpt = obj->getString("type");
              if (!idxOpt || !typeOpt)
                continue;
              members[(unsigned)*idxOpt] = typeOpt->str();
            }
            StructMap[structName] = std::move(members);
          }
        }
      }
    }

    return true;
  }

  // /path/to/foo.c -> foo.changed.c ; foo.h -> foo.changed.h (in cwd)
  static std::string makeOutputName(const std::string &filename) {
    size_t slash = filename.find_last_of('/');
    std::string base =
        (slash != std::string::npos) ? filename.substr(slash + 1) : filename;
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos)
      return base.substr(0, dot) + ".changed" + base.substr(dot);
    return base + ".changed";
  }

  ASTContext *Ctx;
  std::string ConfigPath;
  std::string ConfigContents;
  std::map<std::string, std::string> LocalMap;
  std::map<std::string, std::string> GlobalMap;
  std::map<std::string, std::map<unsigned, std::string>> StructMap;
  std::set<FileID> ModifiedFiles;
};

class ChangeVarASTAction : public PluginASTAction {
public:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef) override {
    return std::make_unique<ChangeVarASTConsumer>(&CI.getASTContext(),
                                                   ConfigPath);
  }

  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string> &args) override {
    ConfigPath = args.empty() ? std::string("config.json") : args[0];
    return true;
  }

private:
  std::string ConfigPath = "config.json";
};

} // namespace

static FrontendPluginRegistry::Add<ChangeVarASTAction> X(
    "change-var-ast",
    "Change variable declaration precision via AST source rewriting (config-driven)");
