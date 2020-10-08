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
    int returnVal;
    Stmt *mPC;

public:
    StackFrame() : mVars(), mExprs(), mPC(), returnVal(0)
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

    inline int getReturnVal()
    {
        return returnVal;
    }
    inline void setReturnVal(Expr *returnExpr)
    {
        returnVal = getStmtVal(returnExpr);
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
    InterpreterVisitor *mVisitor;

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

    int getStmtVal(Stmt *stmt)
    {
        return mStack.back().getStmtVal(stmt);
    }

    void setStmtVal(Stmt *stmt, int val)
    {
        mStack.back().bindStmt(stmt, val);
    }

    void call(CallExpr *callexpr);

    int getReturnVal()
    {
        return mStack.back().getReturnVal();
    }

    void setReturnVal(Expr *returnExpr)
    {
        mStack.back().setReturnVal(returnExpr);
    }

    /// !TODO Support comparison operation
    void binop(BinaryOperator *bop)
    {
        Expr *left = bop->getLHS();
        Expr *right = bop->getRHS();

        if (bop->isAssignmentOp())
        {
            int val = mStack.back().getStmtVal(right);
            setStmtVal(left, val);
            if (DeclRefExpr *declexpr = dyn_cast<DeclRefExpr>(left))
            {
                Decl *decl = declexpr->getFoundDecl();
                setDeclVal(decl, val);
            }
        }
        else
        { //+ - * / > < ==
            int leftVal, rightVal;
            leftVal = mStack.back().getStmtVal(left);
            rightVal = mStack.back().getStmtVal(right);
            switch (bop->getOpcode())
            {
            case BO_Add:
            {
                setStmtVal(bop, leftVal + rightVal);
                break;
            }
            case BO_Sub:
            {
                setStmtVal(bop, leftVal - rightVal);
                break;
            }
            case BO_Mul:
            {
                setStmtVal(bop, leftVal * rightVal);
                break;
            }
            case BO_Div:
            {
                setStmtVal(bop, leftVal / rightVal);
                break;
            }
            case BO_LT:
            {
                int val = leftVal < rightVal ? 1 : 0;
                setStmtVal(bop, val);
                break;
            }
            case BO_GT:
            {
                int val = leftVal > rightVal ? 1 : 0;
                setStmtVal(bop, val);
                break;
            }
            case BO_EQ:
            {
                int val = leftVal == rightVal ? 1 : 0;
                setStmtVal(bop, val);
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
        setStmtVal(integer, val);
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
            setStmtVal(declref, val);
        }
    }

    void cast(CastExpr *castexpr)
    {
        mStack.back().setPC(castexpr);
        if (castexpr->getType()->isIntegerType())
        {
            Expr *expr = castexpr->getSubExpr();
            int val = mStack.back().getStmtVal(expr);
            setStmtVal(castexpr, val);
        }
    }

    /// !TODO Support Function Call
};
