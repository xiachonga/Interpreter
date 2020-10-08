//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

#include "Environment.h"

class InterpreterVisitor : public EvaluatedExprVisitor<InterpreterVisitor>
{
public:
    explicit InterpreterVisitor(const ASTContext &context, Environment *env)
        : EvaluatedExprVisitor(context), mEnv(env) {}
    virtual ~InterpreterVisitor() {}

    virtual void VisitIntegerLiteral(IntegerLiteral *integer)
    {
        int val = integer->getValue().getSExtValue();
        mEnv->setStmtVal(integer, val);
    }

    virtual void VisitIfStmt(IfStmt *ifStmt)
    {
        Expr *cond = ifStmt->getCond();
        Visit(cond);
        if (mEnv->getStmtVal(cond))
        {
            Visit(ifStmt->getThen());
        }
        else if (ifStmt->hasElseStorage())
        {
            Visit(ifStmt->getElse());
        }
    }

    virtual void VisitReturnStmt(ReturnStmt *returnStmt)
    {
        Expr *returnExpr = returnStmt->getRetValue();
        Visit(returnExpr);
        if (returnExpr->getType()->isIntegerType())
        {
            mEnv->setReturnVal(returnExpr);
        }
    }

    virtual void VisitBinaryOperator(BinaryOperator *bop)
    {
        VisitStmt(bop);
        mEnv->binop(bop);
    }

    virtual void VisitDeclRefExpr(DeclRefExpr *declRefExpr)
    {
        VisitStmt(declRefExpr);
        if (declRefExpr->getType()->isIntegerType())
        {
            Decl *decl = declRefExpr->getFoundDecl();
            int val = mEnv->getDeclVal(decl);
            mEnv->setStmtVal(declRefExpr, val);
        }
    }

    virtual void VisitCastExpr(CastExpr *castExpr)
    {
        VisitStmt(castExpr);
        if (castExpr->getType()->isIntegerType())
        {
            Expr *subExpr = castExpr->getSubExpr();
            int val = mEnv->getStmtVal(subExpr);
            mEnv->setStmtVal(castExpr, val);
        }
    }
    
    virtual void VisitCallExpr(CallExpr *callExpr)
    {
        VisitStmt(callExpr);
        FunctionDecl *callee = callExpr->getDirectCallee();
        if (!mEnv->isSpecialCallee(callExpr, callee))
        {
            mEnv->beforeCall(callExpr, callee);
            VisitStmt(callee->getBody());
            mEnv->afterCall(callExpr, callee);
        }
    }

    void AddVarDecl(VarDecl *varDecl)
    {
        mEnv->setDeclVal(varDecl, 0);
        if (varDecl->hasInit())
        {
            Expr *init = varDecl->getInit();
            Visit(init);
            int val = mEnv->getStmtVal(init);
            mEnv->setDeclVal(varDecl, val);
        }
    }

    virtual void VisitDeclStmt(DeclStmt *declStmt)
    {
        for (Decl *decl : declStmt->decls())
        {
            if (VarDecl *varDecl = dyn_cast<VarDecl>(decl))
                AddVarDecl(varDecl);
        }
    }

private:
    Environment *mEnv;
};

class InterpreterConsumer : public ASTConsumer
{
public:
    explicit InterpreterConsumer(const ASTContext &context) : mEnv(),
                                                              mVisitor(context, &mEnv)
    {
    }
    virtual ~InterpreterConsumer() {}

    virtual void HandleTranslationUnit(clang::ASTContext &Context)
    {
        TranslationUnitDecl *unitDecl = Context.getTranslationUnitDecl();
        mEnv.init();
        for (Decl *decl : unitDecl->decls())
        {
            if (FunctionDecl *functionDecl = dyn_cast<FunctionDecl>(decl))
            {
                mEnv.setFunctionDecl(functionDecl);
            }
            else if (VarDecl *varDecl = dyn_cast<VarDecl>(decl))
            {
                mVisitor.AddVarDecl(varDecl);
            }
        }
        if (FunctionDecl *entry = mEnv.getEntry())
        {
            mVisitor.VisitStmt(entry->getBody());
        }
    }

private:
    Environment mEnv;
    InterpreterVisitor mVisitor;
};

class InterpreterClassAction : public ASTFrontendAction
{
public:
    virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance &Compiler, llvm::StringRef InFile)
    {
        return std::unique_ptr<clang::ASTConsumer>(
            new InterpreterConsumer(Compiler.getASTContext()));
    }
};

int main(int argc, char **argv)
{
    if (argc > 1)
    {
        clang::tooling::runToolOnCode(std::unique_ptr<clang::FrontendAction>(new InterpreterClassAction), argv[1]);
    }
}
