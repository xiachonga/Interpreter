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
    int returnFlag;
public:
    StackFrame() : mVars(), mExprs(), mPC(), returnFlag(0)
    {
    }
    void setAlreadyReturn() {
        returnFlag = 1;
    }
    int getAlreadyReturn() {
        return returnFlag;
    }
    bool hasDeclVal(Decl *decl) 
    {
        return mVars.find(decl) != mVars.end();
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
class Heap {
    std::map<int, int> heap;
    std::map<int, int> interval;
    int curr;
public:
   Heap() : heap(), interval(), curr(0) {}
   int Malloc(int size) 
   {
       for (int i = 0; i < size; i ++)
       {
           heap[curr + i] = 0; 
       }
       int base = curr;
       curr += size;
       interval[base] = curr;
       return base;
   }
   void Free (int addr)
   {
       std::map<int,int>::iterator begin = heap.find(addr);
       std::map<int,int>::iterator end = heap.find(interval[addr]);
       heap.erase(begin, end); 
   }
   void Update(int addr, int val) 
   {
       assert(heap.find(addr) != heap.end());
       heap[addr] = val;
   }
   int get(int addr)
   {
       return heap[addr];
   }
};

class Environment
{
    std::vector<StackFrame> mStack;
    Heap mHeap;

    FunctionDecl *mFree; /// Declartions to the built-in functions
    FunctionDecl *mMalloc;
    FunctionDecl *mInput;
    FunctionDecl *mOutput;

    FunctionDecl *mEntry;

public:
    /// Get the declartions to the built-in functions
    Environment() : mStack(), mHeap(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL), mEntry(NULL)
    {
    }
    std::vector<StackFrame> getStack() 
    {
        return mStack;    
    }
    void setAlreadyReturn() {
        mStack.back().setAlreadyReturn();
    }
    bool alreadyReturn() {
        int flag = mStack.back().getAlreadyReturn();
        if (flag == 0) {
            return false;
        } else {
            return true;
        }
    }
    int mallocHeap(int size)
    {
        return mHeap.Malloc(size);
    }
    void updateHeap(int base, int val)
    {
        mHeap.Update(base, val);
    }
    void pushStack() 
    {
        mStack.push_back(StackFrame());     
    }
    void popStack()
    {
        mStack.pop_back();
    }
    bool isSpecialCall(FunctionDecl *callee)
    {
        if (callee == mInput || callee == mOutput || callee == mMalloc || callee == mFree)
        {
            return true;
        } 
        else
        {
            return false;
        }
        
    }
    void addDecl(Decl *decl, int val) 
    {
        mStack.back().bindDecl(decl, val);
    }
    void addStmt(Stmt *stmt, int val)
    {
        mStack.back().bindStmt(stmt, val);
    }
    void setPC(Stmt *stmt) 
    {
        mStack.back().setPC(stmt);
    }
    int getDeclVal(Decl *decl) 
    {
        if (mStack.back().hasDeclVal(decl)) 
        {
            return mStack.back().getDeclVal(decl);
        }
        else
        {
            return mStack.front().getDeclVal(decl);
        }
        
    }
    void setFree(FunctionDecl *mFree) 
    {
        this->mFree = mFree;
    }
    void setMalloc(FunctionDecl *mMalloc) 
    {
        this->mMalloc = mMalloc;
    }
    void setInput(FunctionDecl *mInput) 
    {
        this->mInput = mInput;
    }
    void setOutput(FunctionDecl *mOutput) 
    {
        this->mOutput = mOutput;
    }
    void setEntry(FunctionDecl *mEntry) 
    {
        this->mEntry = mEntry;
    }
    /// Initialize the Environment
    void init()
    {
        mStack.push_back(StackFrame());
    }

    FunctionDecl *getEntry()
    {
        return mEntry;
    }


    /// execStmt
    void unaryOp(UnaryOperator *expr) 
    {
        Expr* subExpr = expr->getSubExpr();
        int val = mStack.back().getStmtVal(subExpr);
        switch (expr->getOpcode())
        {
        case UO_Minus:
            mStack.back().bindStmt(expr, -val);
            break;
        case UO_Deref:
        {
            val = mHeap.get(val);
            mStack.back().bindStmt(expr, val);
            break;
        }
        default:
            break;
        }
        
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
            //update ref value
            if (DeclRefExpr *declexpr = dyn_cast<DeclRefExpr>(left))
            {
                Decl *decl = declexpr->getFoundDecl();
                if (mStack.back().hasDeclVal(decl))
                {
                    mStack.back().bindDecl(decl, val);
                }
                else
                {
                    mStack.front().bindDecl(decl, val);
                }
                
            } 
            else if (ArraySubscriptExpr* arrayExpr = dyn_cast<ArraySubscriptExpr>(left))
            {
                int base = mStack.back().getStmtVal(arrayExpr->getBase());
                int idx = mStack.back().getStmtVal(arrayExpr->getIdx());
                mHeap.Update(base + idx, val);
            } 
            else if (UnaryOperator* unop = dyn_cast<UnaryOperator>(left)) 
            {
                int base = mStack.back().getStmtVal(unop->getSubExpr());
                //cout<<val<<endl;
                mHeap.Update(base, val);
            }
        }
        else
        { //+ - * / > < == 
          //TODO <= >= !=  
            int leftVal, rightVal;
            if (DeclRefExpr *declexpr = dyn_cast<DeclRefExpr>(left))
            {
                Decl *decl = declexpr->getFoundDecl();
                leftVal = getDeclVal(decl);
            }
            else if (left->getType()->isPointerType())
            {
                if (ImplicitCastExpr* imp = dyn_cast<ImplicitCastExpr>(left))
                {
                    leftVal = mStack.back().getStmtVal(imp->getSubExpr());
                }
                    
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
    void execArray(ArraySubscriptExpr *array) 
    {
        int base = mStack.back().getStmtVal(array->getBase());
        int idx = mStack.back().getStmtVal(array->getIdx());
        int val = mHeap.get(base + idx);
        mStack.back().bindStmt(array, val);
    }
    void execSizeOf(UnaryExprOrTypeTraitExpr* expr)
    {
        mStack.back().bindStmt(expr, 1);
    }
    void execParenExpr(ParenExpr* expr)
    {
        int val = mStack.back().getStmtVal(expr->getSubExpr());
        mStack.back().bindStmt(expr, val);
    }
    void integerLiteral(IntegerLiteral *integer)
    {
        int val = integer->getValue().getSExtValue();
        mStack.back().bindStmt(integer, val);
    }
    void declref(DeclRefExpr *declref)
    {
        //mStack.back().setPC(declref);
        if (declref->getType()->isIntegerType() || declref->getType()->isConstantArrayType())
        {
            Decl *decl = declref->getFoundDecl();
            int val = getDeclVal(decl);
            mStack.back().bindStmt(declref, val);
        }
        else if (declref->getType()->isPointerType())
        {
            Decl *decl = declref->getFoundDecl();
            int base = getDeclVal(decl);
            //int val = mHeap.get(base);
            mStack.back().bindStmt(declref, base);
        }
    }
    
    void cast(CastExpr *castexpr)
    {
        //mStack.back().setPC(castexpr);
        if (castexpr->getType()->isFunctionPointerType())
        {
            return;
        }
        else if (castexpr->getType()->isIntegerType() || castexpr->getType()->isPointerType())
        {
            Expr *expr = castexpr->getSubExpr();
            int val = mStack.back().getStmtVal(expr);
            mStack.back().bindStmt(castexpr, val);
        } 
    }
 
    /// !TODO Support Function Call
    void specialCall(CallExpr *callexpr)
    {
        //mStack.back().setPC(callexpr);
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
            cout<<endl;
        }
        else if (callee == mMalloc)
        {
            Expr* arg = callexpr->getArg(0);
            int size = mStack.back().getStmtVal(arg);
            int base = mHeap.Malloc(size);
            mStack.back().bindStmt(callexpr, base);
            /// You could add your code here for Function call Return
        }
        else if (callee == mFree)
        {
            Expr* arg = callexpr->getArg(0);
            int size = mStack.back().getStmtVal(arg);
            mHeap.Free(size);
            return;
        }
    }
};
