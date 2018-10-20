//===- BitypeUtil.h - helper functions and classes for Bitype ---------===//
////
////                     The LLVM Compiler Infrastructure
////
//// This file is distributed under the University of Illinois Open Source
//// License. See LICENSE.TXT for details.
////
////===--------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_BITYPE_H
#define LLVM_TRANSFORMS_UTILS_BITYPE_H

#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/ErrorOr.h"
#include <map>
#include <set>


#define MAXNODE 1000000
#define STACKALLOC 1
#define HEAPALLOC 2
#define GLOBALALLOC 3
#define REALLOC 4
#define PLACEMENTNEW 5
#define REINTERPRET 6

#define CONOBJADD 1
#define VLAOBJADD 2
#define CONOBJDEL 3
#define VLAOBJDEL 4

#define BIT_LOG
#define BIT_DEBUG

using std::string;
namespace llvm{

    // struct bitypeCode {
    //     unsigned index;
    //     unsigned sum;
    // };

    class TypeDetailInfo{
        public:
            uint32_t TypeIndex;
            string TypeName;
    };

    class TypeInfo{
        public:
            StructType* StructTy;
            TypeDetailInfo DetailInfo;
            uint32_t ElementSize;
            std::vector<TypeDetailInfo> DirectParents;
            std::vector<TypeDetailInfo> DirectPhantomTypes;
            std::vector<TypeDetailInfo> AllParents;
            std::vector<TypeDetailInfo> AllPhantomTypes;
    };

    extern cl::opt<string> ClCodeMapFile;
    extern cl::opt<string> ClCastRelatedFile;
    extern cl::opt<string> ClInheritanceFile;
    extern cl::opt<string> ClInheritanceAllFile;

    extern cl::opt<bool> ClMakeLogInfo;

    extern cl::opt<bool> ClSafeStackOpt;
    extern cl::opt<bool> ClStackOpt;

    extern cl::opt<bool> ClCompileTimeVerifyOpt;
    extern cl::opt<bool> ClInlineOpt;
   extern cl::opt<bool> ClMakeLogInfo;

    
    
    typedef std::set<StructType *> StructOffsetZeroSet;
    typedef std::map<Function *, std::vector<Instruction *> *> FunctionReturnTy;
    typedef std::map<uint64_t, StructType*> StructElementInfoTy;
    typedef std::map<uint64_t, std::set<StructType*>> StructElementSetInfoTy;
    
    class BitypeGetConfig{
        public:
        string getCodeMapPath();
        string getCastRelatedPath();
    };
    
    class BitypeUtil{
        
        public:
            std::map<string, int> bitypeCodeMap;
            std::map<string, int> bitypeCastRelatedClass;
            std::map<string, std::set<string>>  BitypeInheritanceSet;
            std::map<string, std::set<string>> BitypeInheritanceAllSet;
            std::map<unsigned, string> bitypeClassIndexSet;
            std::map<string, unsigned> bitypeClassIndexSet2;

            std::map<string, StructType *> classStructSet;
            
            std::map<string, GlobalVariable *> GlobalVariableMap;
            const DataLayout &DL;
            bool VisitCheck[MAXNODE];

            std::vector<TypeInfo> AllTypeInfo;
            uint32_t AllTypeNum = 0;
            Type *VoidTy;
            Type *Int8PtrTy;
            Type *Int32PtrTy;
            Type *Int64PtrTy;
            Type *Int1Ty;
            Type *Int8Ty;
            Type *Int32Ty;
            Type *Int64Ty;
            Type *IntptrTyN;
            Type *IntptrTy;

            BitypeUtil(const DataLayout &DL) : DL(DL){}
            int readCodeMap();
            int readRelatedClass();
            int readInheritanceSet();
            int readInheritanceAllSet();
            void syncTypeName(string &);
            bool isInterestingStructType(StructType *);
            bool isInterestingArrayType(ArrayType *);
            bool isInterestingType(Type *);
            bool isInCastRelatedSetType(Type *);
            bool isInCastRelatedSetArrayType(ArrayType *);
            bool isInCastRelatedSetStructType(StructType *);
            void syncModuleName(string &);
            void InitType(Module &);
            void InitReadFiles();
            void getStructOffsets(StructType *, StructElementInfoTy &, uint32_t);
            void getArrayOffsets(Type *, StructElementInfoTy &, uint32_t);
            void getArrayAlignment(Type*, StructElementSetInfoTy &, uint32_t, std::set<uint32_t>&);
            void getStructAlignment(StructType*, StructElementSetInfoTy &, uint32_t, std::set<uint32_t>&);
            void getOffetZeroStruct(StructType *, StructOffsetZeroSet&);
            void getOffsetZeroArray(Type *, StructOffsetZeroSet &);
            void getAllClassSafeCast(Module &, string);
            void computeCode(std::set<int>, std::vector<unsigned char> &, int);  
            void getGlobalName(string, string, string, string &);
            void getGlobalNameSet(string, string, std::set<StructType*>, string&);
            bool isInCastRelatedSet(StructType *);
            void getDirectTypeInfo(Module&);
            void extendParentSet(int TargetIndex, int CurrentIndex);
            void extendPhantomSet(int TargetIndex, int CurrentIndex);
            void setTypeDetailInfo(StructType *STy, TypeDetailInfo &TargetDetailInfo, uint32_t AllTypeNum);
            void extendTypeRelationInfo();
            void parsingTypeInfo(StructType *STy, TypeInfo &NewType, uint32_t AllTypeNum);
            void extendSafeNum(std::set<int> &safeNum, const string stName, int& currentFamilySize);
            GlobalVariable* CreateGlobalVariable(string prefix, string,Module &, std::set<StructType*> relatedCast);
            // bool typeIsInCastRelatedSet(Type *);
            // bool arrayIsInCastRelatedSet(ArrayType *);  
            Instruction* findNextInstruction(Instruction *CurInst);
            AllocaInst *findAllocaForValue(Value *);
            void emitRemoveInst(string, Module *, IRBuilder<> &, AllocaInst *);

            bool isInterestingFn(Function *);

            bool isSafeStackAlloca(AllocaInst *);


            void insertUpdate(string, Module *, IRBuilder<> &, uint32_t, Value *, StructElementSetInfoTy &, 
                                uint32_t, Value *, Value *, BasicBlock *);
            void emitInstForObjTrace(string prefix, Module *, IRBuilder<> &, StructElementSetInfoTy &, uint32_t, Value *, 
            Value *, uint32_t, uint32_t, uint32_t, Value *, BasicBlock*);        

            void insertRemove(string prefix, Module *, IRBuilder<> &, uint32_t, Value *,
                            StructElementSetInfoTy &, Value *, int, BasicBlock *);

            int getTwoSize(int);
            //string getCodeMapPath();

            GlobalVariable *getLookUpStart(Module &);
            GlobalVariable *getLookUpSecondLevelGlobal(Module &, string);
    };
}

#endif