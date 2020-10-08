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
    std::vector<StackFrame> mStack;

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

    // Global vars stack or main function stack
    void init()
    {
        mStack.push_back(StackFrame());
    }

    void setFunctionDecl(FunctionDecl *functionDecl)
    {
        if (functionDecl->getName().equals("FREE"))
            mFree = functionDecl;
        else if (functionDecl->getName().equals("MALLOC"))
            mMalloc = functionDecl;
        else if (functionDecl->getName().equals("GET"))
            mInput = functionDecl;
        else if (functionDecl->getName().equals("PRINT"))
            mOutput = functionDecl;
        else if (functionDecl->getName().equals("main"))
            mEntry = functionDecl;
    }

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

    inline bool isSpecialCallee(CallExpr *callExpr, FunctionDecl *callee)
    {
        if (callee == mInput)
        {
            int val = 0;
            llvm::errs() << "Please Input an Integer Value : ";
            scanf("%d", &val);
            mStack.back().bindStmt(callExpr, val);
            return true;
        }
        else if (callee == mOutput)
        {
            int val;
            Expr *decl = callExpr->getArg(0);
            val = mStack.back().getStmtVal(decl);
            llvm::errs() << val;
            return true;
        }
        else if (callee == mMalloc)
        {
            return true;
        }
        else if (callee == mFree)
        {
            return true;
        }
        return false;
    }

    inline void beforeCall(CallExpr *callExpr, FunctionDecl *callee)
    {
        std::vector<int> args;
        for (Expr *arg : callExpr->arguments())
        {
            args.push_back(getStmtVal(arg));
        }
        args = std::vector<int>(args.rbegin(), args.rend());
        mStack.push_back(StackFrame());
        for (VarDecl *varDecl : callee->parameters())
        {
            setDeclVal(varDecl, args.back());
            args.pop_back();
        }
    }

    inline void afterCall(CallExpr *callExpr, FunctionDecl *callee)
    {
        if (callee->getReturnType()->isIntegerType())
        {
            int val = getReturnVal();
            mStack.pop_back();
            setStmtVal(callExpr, val);
        }
        else
            mStack.pop_back();
    }

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
            int leftVal = mStack.back().getStmtVal(left);
            int rightVal = mStack.back().getStmtVal(right);
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
};
