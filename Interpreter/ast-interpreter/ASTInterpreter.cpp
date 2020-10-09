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

    virtual void VisitForStmt(ForStmt *forStmt)
    {
        Stmt *init = forStmt->getInit();
        Expr *cond = forStmt->getCond();
        Expr *inc = forStmt->getInc();
        for (Visit(init), Visit(cond); mEnv->getStmtVal(cond); Visit(inc), Visit(cond))
            Visit(forStmt->getBody());
    }

    virtual void VisitWhileStmt(WhileStmt *whileStmt)
    {
        Expr *cond = whileStmt->getCond();
        Visit(cond);
        while (mEnv->getStmtVal(cond))
        {
            Visit(whileStmt->getBody());
            Visit(cond);
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

    virtual void VisitUnaryOperator(UnaryOperator *unaryOp)
    {
        VisitStmt(unaryOp);
        int val = mEnv->getStmtVal(unaryOp->getSubExpr());
        switch (unaryOp->getOpcode())
        {
        case UO_Minus:
            mEnv->setStmtVal(unaryOp, -val);
            break;
        case UO_Deref:
            mEnv->setStmtVal(unaryOp, val);
            break;
        default:
            assert(0);
            break;
        }
    }

    virtual void VisitBinaryOperator(BinaryOperator *bop)
    {
        VisitStmt(bop);
        mEnv->binop(bop);
    }

    virtual void VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *sizeofExpr)
    {
        VisitStmt(sizeofExpr);
        if (sizeofExpr->getKind() == UETT_SizeOf)
        {
            mEnv->setStmtVal(sizeofExpr, 1);
        }
    }

    virtual void VisitDeclRefExpr(DeclRefExpr *declRefExpr)
    {
        VisitStmt(declRefExpr);
        Decl *decl = declRefExpr->getFoundDecl();
        if (VarDecl *varDecl = dyn_cast<VarDecl>(decl))
        {
            int val = mEnv->getDeclVal(decl);
            mEnv->setStmtVal(declRefExpr, val);
        }
    }

    virtual void VisitCastExpr(CastExpr *castExpr)
    {
        VisitStmt(castExpr);
        if (castExpr->getCastKind() != CK_FunctionToPointerDecay)
        {
            Expr *subExpr = castExpr->getSubExpr();
            int val = mEnv->getStmtVal(subExpr);
            if (castExpr->getCastKind() == CK_LValueToRValue)
            {
                val = mEnv->getRightVal(val);
            }
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
        if (varDecl->getType()->isIntegerType())
        {
            if (varDecl->hasInit())
            {
                Expr *init = varDecl->getInit();
                Visit(init);
                int val = mEnv->getStmtVal(init);
                mEnv->setDeclVal(varDecl, val);
            }
            return;
        }
        if (const ConstantArrayType *type = dyn_cast<ConstantArrayType>(varDecl->getType().getTypePtr()))
        {
            int size = type->getSize().getSExtValue();
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
