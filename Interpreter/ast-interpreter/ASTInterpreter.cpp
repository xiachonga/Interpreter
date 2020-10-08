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
        mEnv->integerLiteral(integer);
    }

    virtual void VisitIfStmt(IfStmt *ifStmt)
    {
        Expr *cond = ifStmt->getCond();
        Visit(cond);
        if (mEnv->getCond(cond))
        {
            Visit(ifStmt->getThen());
        }
        else if (ifStmt->hasElseStorage())
        {
            Visit(ifStmt->getElse());
        }
    }

    virtual void VisitBinaryOperator(BinaryOperator *bop)
    {
        VisitStmt(bop);
        mEnv->binop(bop);
    }
    virtual void VisitDeclRefExpr(DeclRefExpr *expr)
    {
        VisitStmt(expr);
        mEnv->declref(expr);
    }
    virtual void VisitCastExpr(CastExpr *expr)
    {
        VisitStmt(expr);
        mEnv->cast(expr);
    }
    virtual void VisitCallExpr(CallExpr *call)
    {
        VisitStmt(call);
        mEnv->call(call);
    }
    virtual void VisitDeclStmt(DeclStmt *declstmt)
    {
        mEnv->decl(declstmt);
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
        TranslationUnitDecl *decl = Context.getTranslationUnitDecl();
        mEnv.init(decl, &mVisitor);
        FunctionDecl *entry = mEnv.getEntry();
        mVisitor.VisitStmt(entry->getBody());
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

void Environment::init(TranslationUnitDecl *unit, InterpreterVisitor *mVisitor)
{
    // Global vars in this stack
    mStack.push_back(StackFrame());
    for (TranslationUnitDecl::decl_iterator i = unit->decls_begin(), e = unit->decls_end(); i != e; ++i)
    {
        if (FunctionDecl *fdecl = dyn_cast<FunctionDecl>(*i))
        {
            if (fdecl->getName().equals("FREE"))
                mFree = fdecl;
            else if (fdecl->getName().equals("MALLOC"))
                mMalloc = fdecl;
            else if (fdecl->getName().equals("GET"))
                mInput = fdecl;
            else if (fdecl->getName().equals("PRINT"))
                mOutput = fdecl;
            else if (fdecl->getName().equals("main"))
                mEntry = fdecl;
            else
            {
            }
        }
        else if (VarDecl *varDecl = dyn_cast<VarDecl>(*i))
        {
            mStack.back().bindDecl(varDecl, 0);
            if (varDecl->hasInit())
            {
                Expr *init = varDecl->getInit();
                mVisitor->Visit(init);
                int val = mStack.back().getStmtVal(init);
                setDeclVal(varDecl, val);
            }
        }
    }
    // main function frame
    mStack.push_back(StackFrame());
}
