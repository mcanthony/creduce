#include "RenameFun.h"

#include <sstream>

#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"

#include "TransformationManager.h"
#include "RewriteUtils.h"

using namespace clang;
using namespace llvm;

static const char *DescriptionMsg =
"Another pass to increase readability of reduced code. \
It renames function names to fn1, fn2, ...\n";

static RegisterTransformation<RenameFun>
         Trans("rename-fun", DescriptionMsg);

class RNFunCollectionVisitor : public 
  RecursiveASTVisitor<RNFunCollectionVisitor> {

public:

  explicit RNFunCollectionVisitor(RenameFun *Instance)
    : ConsumerInstance(Instance)
  { }

  bool VisitFunctionDecl(FunctionDecl *FD);

  bool VisitCallExpr(CallExpr *CE);

private:

  RenameFun *ConsumerInstance;

};

class RenameFunVisitor : public RecursiveASTVisitor<RenameFunVisitor> {
public:

  explicit RenameFunVisitor(RenameFun *Instance)
    : ConsumerInstance(Instance)
  { }

  bool VisitFunctionDecl(FunctionDecl *FD);

  bool VisitCallExpr(CallExpr *CE);

private:

  RenameFun *ConsumerInstance;

};

bool RNFunCollectionVisitor::VisitFunctionDecl(FunctionDecl *FD)
{
  FunctionDecl *CanonicalFD = FD->getCanonicalDecl();
  ConsumerInstance->addFun(CanonicalFD);
  if (!ConsumerInstance->hasValidPostfix(FD->getNameAsString()))
    ConsumerInstance->HasValidFuns = true;
  return true;
}

bool RNFunCollectionVisitor::VisitCallExpr(CallExpr *CE)
{
  FunctionDecl *FD = CE->getDirectCallee();
  FunctionDecl *CanonicalFD = FD->getCanonicalDecl();

  // This case is handled by VisitFunctionDecl
  if (CanonicalFD->isDefined())
    return true;

  // It's possible we don't have function definition
  ConsumerInstance->addFun(CanonicalFD);
  if (!ConsumerInstance->hasValidPostfix(FD->getNameAsString()))
    ConsumerInstance->HasValidFuns = true;
  return true;
}

bool RenameFunVisitor::VisitFunctionDecl(FunctionDecl *FD)
{
  FunctionDecl *CanonicalDecl = FD->getCanonicalDecl();
  llvm::DenseMap<FunctionDecl *, std::string>::iterator I = 
    ConsumerInstance->FunToNameMap.find(CanonicalDecl);

  TransAssert((I != ConsumerInstance->FunToNameMap.end()) &&
              "Cannot find FunctionDecl!");

  return RewriteUtils::replaceFunctionDeclName(FD, (*I).second,
           &ConsumerInstance->TheRewriter, ConsumerInstance->SrcManager);
}

bool RenameFunVisitor::VisitCallExpr(CallExpr *CE)
{
  FunctionDecl *FD = CE->getDirectCallee();
  FunctionDecl *CanonicalDecl = FD->getCanonicalDecl();
  llvm::DenseMap<FunctionDecl *, std::string>::iterator I = 
    ConsumerInstance->FunToNameMap.find(CanonicalDecl);

  TransAssert((I != ConsumerInstance->FunToNameMap.end()) &&
              "Cannot find FunctionDecl!");
  return !ConsumerInstance->TheRewriter.ReplaceText(CE->getLocStart(), 
            FD->getNameAsString().size(), (*I).second);
}

void RenameFun::Initialize(ASTContext &context) 
{
  Context = &context;
  SrcManager = &Context->getSourceManager();
  FunCollectionVisitor = new RNFunCollectionVisitor(this);
  RenameVisitor = new RenameFunVisitor(this);
  TheRewriter.setSourceMgr(Context->getSourceManager(), 
                           Context->getLangOptions());
}

void RenameFun::HandleTopLevelDecl(DeclGroupRef D) 
{
  for (DeclGroupRef::iterator I = D.begin(), E = D.end(); I != E; ++I) {
    FunCollectionVisitor->TraverseDecl(*I);
  }
}

void RenameFun::HandleTranslationUnit(ASTContext &Ctx)
{
  if (QueryInstanceOnly) {
    if (HasValidFuns)
      ValidInstanceNum = 1;
    else
      ValidInstanceNum = 0;
    return;
  }

  if (!HasValidFuns) {
    TransError = TransNoValidFunsError;
    return;
  }

  TransAssert(RenameVisitor && "NULL RenameVisitor!");
  Ctx.getDiagnostics().setSuppressAllDiagnostics(false);

  RenameVisitor->TraverseDecl(Ctx.getTranslationUnitDecl());

  if (Ctx.getDiagnostics().hasErrorOccurred() ||
      Ctx.getDiagnostics().hasFatalErrorOccurred())
    TransError = TransInternalError;
}

bool RenameFun::hasValidPostfix(const std::string &Name)
{
  unsigned int Value;

  if (Name.size() <= 2)
    return false;

  std::string Prefix = Name.substr(0, 2);
  if (Prefix != FunNamePrefix)
    return false;

  std::string RestStr = Name.substr(2);
  std::stringstream TmpSS(RestStr);
  if (!(TmpSS >> Value))
    return false;

  return true;
}
void RenameFun::addFun(FunctionDecl *FD)
{
  if (FunToNameMap.find(FD) != FunToNameMap.end())
    return;

  std::stringstream SS;

  FunNamePostfix++;
  SS << FunNamePrefix << FunNamePostfix;

  TransAssert((FunToNameMap.find(FD) == FunToNameMap.end()) &&
              "Duplicated Fun name!");

  FunToNameMap[FD] = SS.str();
}

RenameFun::~RenameFun(void)
{
  if (FunCollectionVisitor)
    delete FunCollectionVisitor;
  if (RenameVisitor)
    delete RenameVisitor;
}

