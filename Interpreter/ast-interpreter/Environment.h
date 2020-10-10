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
    void bindDecl(Decl *decl, int val)
    {
        mVars[decl] = val;
    }
    int getDeclVal(Decl *decl)
    {
        assert(mVars.find(decl) != mVars.end());
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
    std::vector<StackFrame> getStack() {
        return mStack;    
    }
    void addDecl(Decl *decl, int val) {
        mStack.back().bindDecl(decl, val);
    }
    /// Initialize the Environment
    void init(TranslationUnitDecl *unit)
    {
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
            }
        }
        mStack.push_back(StackFrame());
    }

    FunctionDecl *getEntry()
    {
        return mEntry;
    }

    void unaryOp(UnaryOperator *expr) 
    {
        Expr* subExpr = expr->getSubExpr();
        mStack.back().bindStmt(expr, -(mStack.back().getStmtVal(subExpr)));
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
                mStack.back().bindDecl(decl, val);
            }
        }
        else
        { //+ - * / > < ==
            int leftVal, rightVal;
            if (DeclRefExpr *declexpr = dyn_cast<DeclRefExpr>(left))
            {
                Decl *decl = declexpr->getFoundDecl();
                leftVal = mStack.back().getDeclVal(decl);
            }
            else
            {
                leftVal = mStack.back().getStmtVal(left);
            }
            if (DeclRefExpr *declexpr = dyn_cast<DeclRefExpr>(right))
            {
                Decl *decl = declexpr->getFoundDecl();
                rightVal = mStack.back().getDeclVal(decl);
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
    void declref(DeclRefExpr *declref)
    {
        mStack.back().setPC(declref);
        if (declref->getType()->isIntegerType())
        {
            Decl *decl = declref->getFoundDecl();

            int val = mStack.back().getDeclVal(decl);
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
