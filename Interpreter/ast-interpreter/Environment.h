//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//
#include <stdio.h>

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

class InterpreterVisitor;

class StackFrame
{
    /// StackFrame maps Variable Declaration to Value
    /// Which are either integer or addresses (also represented using an Integer value)
    std::map<Decl *, int> mVars;
    std::map<Stmt *, int> mExprs;
    /// The current stmt
    Stmt *mPC;

public:
    StackFrame() : mVars(), mExprs(), mPC()
    {
    }

    inline int haveDecl(Decl *decl)
    {
        return mVars.count(decl);
    }
    inline void bindDecl(Decl *decl, int val)
    {
        mVars[decl] = val;
    }
    inline int getDeclVal(Decl *decl)
    {
        assert(haveDecl(decl));
        return mVars.find(decl)->second;
    }

    void bindStmt(Stmt *stmt, int val)
    {
        mExprs[stmt] = val;
    }
    int getStmtVal(Stmt *stmt)
    {
        assert(mExprs.find(stmt) != mExprs.end());
        return mExprs[stmt];
    }
    void setPC(Stmt *stmt)
    {
        mPC = stmt;
    }
    Stmt *getPC()
    {
        return mPC;
    }
};

/// Heap maps address to a value
/*
class Heap {
public:
   int Malloc(int size) ;
   void Free (int addr) ;
   void Update(int addr, int val) ;
   int get(int addr);
};
*/

class Environment
{
    std::vector<StackFrame> mStack;
    std::map<Decl *, int> mVars; // Global vars

    FunctionDecl *mFree; /// Declartions to the built-in functions
    FunctionDecl *mMalloc;
    FunctionDecl *mInput;
    FunctionDecl *mOutput;

    FunctionDecl *mEntry;

public:
    /// Get the declartions to the built-in functions
    Environment() : mStack(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL), mEntry(NULL)
    {
    }

    /// Initialize the Environment
    void init(TranslationUnitDecl *, InterpreterVisitor *);

    FunctionDecl *getEntry()
    {
        return mEntry;
    }

    int getDeclVal(Decl *decl)
    {
        if (mStack[0].haveDecl(decl))
            return mStack[0].getDeclVal(decl);
        return mStack.back().getDeclVal(decl);
    }

    void setDeclVal(Decl *decl, int val)
    {
        if (mStack[0].haveDecl(decl))
            mStack[0].bindDecl(decl, val);
        else
            mStack.back().bindDecl(decl, val);
    }

    int getCond(Expr *cond)
    {
        return mStack.back().getStmtVal(cond);
    }

    /// !TODO Support comparison operation
    void binop(BinaryOperator *bop)
    {
        Expr *left = bop->getLHS();
        Expr *right = bop->getRHS();

        if (bop->isAssignmentOp())
        {
            int val = mStack.back().getStmtVal(right);
            mStack.back().bindStmt(left, val);
            if (DeclRefExpr *declexpr = dyn_cast<DeclRefExpr>(left))
            {
                Decl *decl = declexpr->getFoundDecl();
                setDeclVal(decl, val);
            }
        }
        else
        { //+ - * / > < ==
            int leftVal, rightVal;
            if (DeclRefExpr *declexpr = dyn_cast<DeclRefExpr>(left))
            {
                Decl *decl = declexpr->getFoundDecl();
                leftVal = getDeclVal(decl);
            }
            else
            {
                leftVal = mStack.back().getStmtVal(left);
            }
            if (DeclRefExpr *declexpr = dyn_cast<DeclRefExpr>(right))
            {
                Decl *decl = declexpr->getFoundDecl();
                rightVal = getDeclVal(decl);
            }
            else
            {
                rightVal = mStack.back().getStmtVal(right);
            }

            switch (bop->getOpcode())
            {
            case BO_Add:
            {
                mStack.back().bindStmt(bop, leftVal + rightVal);
                break;
            }
            case BO_Sub:
            {
                mStack.back().bindStmt(bop, leftVal - rightVal);
                break;
            }
            case BO_Mul:
            {
                mStack.back().bindStmt(bop, leftVal * rightVal);
                break;
            }
            case BO_Div:
            {
                mStack.back().bindStmt(bop, leftVal / rightVal);
                break;
            }
            case BO_LT:
            {
                int val = leftVal < rightVal ? 1 : 0;
                mStack.back().bindStmt(bop, val);
                break;
            }
            case BO_GT:
            {
                int val = leftVal > rightVal ? 1 : 0;
                mStack.back().bindStmt(bop, val);
                break;
            }
            case BO_EQ:
            {
                int val = leftVal == rightVal ? 1 : 0;
                mStack.back().bindStmt(bop, val);
                break;
            }
            default:
                break;
            }
        }
    }
    void integerLiteral(IntegerLiteral *integer)
    {
        int val = integer->getValue().getSExtValue();
        mStack.back().bindStmt(integer, val);
    }
    void decl(DeclStmt *declstmt)
    {
        for (DeclStmt::decl_iterator it = declstmt->decl_begin(), ie = declstmt->decl_end();
             it != ie; ++it)
        {
            Decl *decl = *it;
            if (VarDecl *vardecl = dyn_cast<VarDecl>(decl))
            {
                setDeclVal(vardecl, 0);
            }
        }
    }
    void declref(DeclRefExpr *declref)
    {
        mStack.back().setPC(declref);
        if (declref->getType()->isIntegerType())
        {
            Decl *decl = declref->getFoundDecl();

            int val = getDeclVal(decl);
            mStack.back().bindStmt(declref, val);
        }
    }

    void cast(CastExpr *castexpr)
    {
        mStack.back().setPC(castexpr);
        if (castexpr->getType()->isIntegerType())
        {
            Expr *expr = castexpr->getSubExpr();
            int val = mStack.back().getStmtVal(expr);
            mStack.back().bindStmt(castexpr, val);
        }
    }

    /// !TODO Support Function Call
    void call(CallExpr *callexpr)
    {
        mStack.back().setPC(callexpr);
        int val = 0;
        FunctionDecl *callee = callexpr->getDirectCallee();
        if (callee == mInput)
        {
            llvm::errs() << "Please Input an Integer Value : ";
            scanf("%d", &val);

            mStack.back().bindStmt(callexpr, val);
        }
        else if (callee == mOutput)
        {
            Expr *decl = callexpr->getArg(0);
            val = mStack.back().getStmtVal(decl);
            llvm::errs() << val;
        }
        else
        {

            /// You could add your code here for Function call Return
        }
    }
};
