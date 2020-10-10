//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

using namespace std;
using namespace clang;

#include "Environment.h"
//zzctest
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
    virtual void VisitUnaryOperator(UnaryOperator *expr)
    {
        VisitStmt(expr);
        mEnv->unaryOp(expr);
    }
    virtual void VisitIfStmt(IfStmt *expr)
    {
        Visit(expr->getCond());
        if (mEnv->getStack().back().getStmtVal(expr->getCond()))
        {
            if (CompoundStmt *then = dyn_cast<CompoundStmt>(expr->getThen()))
            {
                VisitStmt(then);
            }
            else
            {
                Visit(expr->getThen());
            }
        }
        else if (expr->getElse())
        {
            if (CompoundStmt *elseStmt = dyn_cast<CompoundStmt>(expr->getElse()))
            {
                VisitStmt(elseStmt);
            }
            else
            {
                Visit(expr->getElse());
            }
        }
    }
    virtual void VisitForStmt(ForStmt *expr)
    {
        Visit(expr->getInit());

        while (1)
        {
            Visit(expr->getCond());
            if (!mEnv->getStack().back().getStmtVal(expr->getCond()))
            {
                break;
            }

            if (CompoundStmt *body = dyn_cast<CompoundStmt>(expr->getBody()))
            {
                VisitStmt(body);
            }
            else
            {
                Visit(expr->getBody());
            }

            Visit(expr->getInc());
            //TODO add break、continue语句
        }
    }
    virtual void VisitWhileStmt(WhileStmt *expr)
    {
        while (1)
        {
            Visit(expr->getCond());
            if (!mEnv->getStack().back().getStmtVal(expr->getCond()))
            {
                break;
            }
            if (CompoundStmt *body = dyn_cast<CompoundStmt>(expr->getBody()))
            {
                VisitStmt(body);
            }
            else
            {
                Visit(expr->getBody());
            }
            //TODO add break、continue语句
        }
    }
    virtual void VisitCallExpr(CallExpr *call)
    {
        VisitStmt(call);
        mEnv->call(call);
    }
    virtual void VisitDeclStmt(DeclStmt *declstmt)
    {
        //mEnv->decl(declstmt);
        for (DeclStmt::decl_iterator it = declstmt->decl_begin(), ie = declstmt->decl_end();
             it != ie; ++it)
        {
            Decl *decl = *it;
            if (VarDecl *varDecl = dyn_cast<VarDecl>(decl))
            {
                if (varDecl->hasInit())
                {
                    Visit(varDecl->getInit());
                    mEnv->addDecl(varDecl, mEnv->getStack().back().getStmtVal(varDecl->getInit()));
                }
                else
                {
                    mEnv->addDecl(varDecl, 0);
                }
            }
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
        TranslationUnitDecl *decl = Context.getTranslationUnitDecl();
        initEnvironment(decl);
        mEnv.getStack().push_back(StackFrame());
        FunctionDecl *entry = mEnv.getEntry();
        mVisitor.VisitStmt(entry->getBody());
    }

private:
    Environment mEnv;
    InterpreterVisitor mVisitor;

    void initEnvironment(TranslationUnitDecl *unit)
    {
        mEnv.init();
        for (TranslationUnitDecl::decl_iterator i = unit->decls_begin(), e = unit->decls_end(); i != e; ++i)
        {
            if (FunctionDecl *fdecl = dyn_cast<FunctionDecl>(*i))
            {
                if (fdecl->getName().equals("FREE"))
                    mEnv.setFree(fdecl);
                else if (fdecl->getName().equals("MALLOC"))
                    mEnv.setMalloc(fdecl);
                else if (fdecl->getName().equals("GET"))
                    mEnv.setInput(fdecl);
                else if (fdecl->getName().equals("PRINT"))
                    mEnv.setOutput(fdecl);
                else if (fdecl->getName().equals("main"))
                    mEnv.setEntry(fdecl);
            }
            else if (VarDecl *varDecl = dyn_cast<VarDecl>(*i))
            {
                if (varDecl->hasInit())
                {
                    mVisitor.Visit(varDecl->getInit());
                    mEnv.addDecl(varDecl, mEnv.getStack().back().getStmtVal(varDecl->getInit()));
                }
                else
                {
                    mEnv.addDecl(varDecl, 0);
                }
            }
        }
        
    }
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
