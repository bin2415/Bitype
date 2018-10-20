//===-- BitypeInitializePass.cpp -----------------------------------------------===//
//
// This file is a part of HexType, a type confusion detector.
//
// The BitypeInitializePass has below two functions:
//   - Create object relationship information.
//   - Compile time verification
// This pass will run before all optimization passes run
// The rest is handled by the run-time library.
//===----------------------------------------------------------------------===//

#include "llvm/Pass.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/Constants.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/BitypeUtil.h"

#include <cxxabi.h>

#define BITYPE_DEBUG_LOG

using namespace llvm;
using std::string;

namespace{
    
    class BitypeInitialize : public ModulePass{
        public:
            static char ID;
            
            TargetLibraryInfo *tli;
            TargetLibraryInfoImpl tlii;

            BitypeInitialize() : ModulePass(ID){}

            BitypeUtil *bitypeUtils;


    bool isAllocCall(CallInst *val) {
      if (isAllocationFn(val, this->tli) &&
          (isMallocLikeFn(val, this->tli) || isCallocLikeFn(val, this->tli) ||
           !isAllocLikeFn(val, this->tli)))
        return true;
      return false;
    }

    void collectHeapAlloc(CallInst *call,
                        std::map<CallInst *, Type *> *heapObjsNew) {
      std::string functionName;
      if (call->getCalledFunction() != nullptr)
        functionName.assign(call->getCalledFunction()->getName());

      int unmangledStatus;
      char *unmangledName =
        abi::__cxa_demangle(functionName.c_str(), nullptr,
                            nullptr, &unmangledStatus);
      bool isOverloadedNew = false;
      if (unmangledStatus == 0) {
        std::string unmangledNameStr(unmangledName);
        if (unmangledNameStr.find("::operator new(unsigned long)") !=
            std::string::npos ||
            unmangledNameStr.find(
              "::operator new(unsigned long, std::nothrow_t const&)") !=
            std::string::npos ||
            unmangledNameStr.find("::operator new[](unsigned long)") !=
            std::string::npos ||
            unmangledNameStr.find(
              "::operator new[](unsigned long, std::nothrow_t const&)")
            != std::string::npos) {
          isOverloadedNew = true;
        }
      }

      if (isAllocCall(call) || isOverloadedNew)
        if (Type *allocTy = getMallocAllocatedType(call, this->tli))
          if (bitypeUtils->isInterestingType(allocTy))
            heapObjsNew->insert(
              std::pair<CallInst *, Type *>(call, allocTy));

      return;
    }

    void collectFree(CallInst *call, Instruction *InstPrev,
                   std::map<CallInst *, Type *> *heapObjsFree) {
      if (isFreeCall(call, this->tli))
        if (const BitCastInst *BCI = dyn_cast<BitCastInst>(InstPrev))
          if (PointerType *FreeType =
              cast<PointerType>(BCI->getSrcTy())) {
            Type *VTy = FreeType->getElementType();
            if (bitypeUtils->isInterestingType(VTy))
                heapObjsFree->insert(
                  std::pair<CallInst *, Type *>(call, VTy));
          }

      return;
    }

    bool isReallocFn(Function *F) {
      std::string funName = F->getName().str();
      if ((funName.find("realloc") != std::string::npos))
        return true;

      return false;
    }

    void handleHeapAlloc(Module &M, std::map<CallInst *, Type *> *heapObjsNew) {
      
      int index = 0;
      int addedIndex = 0;
      for(std::map<CallInst *, Type *>::iterator it = heapObjsNew->begin();
        it != heapObjsNew->end(); ++it){
          //errs() << "Having new operator\n";
          Instruction *next = bitypeUtils->findNextInstruction(it->first);
          IRBuilder<> Builder(next);

          bool isRealloc = 0;

          StructElementSetInfoTy offsets;
          std::set<uint32_t> hasVisited;
          bitypeUtils->getArrayAlignment(it->second, offsets, 0, hasVisited);
          if(bitypeUtils->isInterestingType(it->second)){
            index++;
          }
          if(offsets.size() == 0)
            continue;
          addedIndex++;
          Value *ArraySize;
          Value *TypeSize;
          Value *ArraySizeF = nullptr;

          if (isMallocLikeFn(it->first, this->tli) ||
            !isAllocLikeFn(it->first, this->tli) ||
            !isAllocationFn(it->first, this->tli)) {
              
          if (isMallocLikeFn(it->first, this->tli) ||
              !isAllocationFn(it->first, this->tli))
            ArraySize = it->first->getArgOperand(0);
          else
            ArraySize = it->first->getArgOperand(1);


          unsigned long TypeSizeVal =
            bitypeUtils->DL.getTypeAllocSize(it->second);
          TypeSize = ConstantInt::get(bitypeUtils->Int64Ty, TypeSizeVal);

          if (TypeSizeVal != 0)
            ArraySizeF = Builder.CreateUDiv(ArraySize, TypeSize);
          else
            ArraySizeF = ArraySize;

          if (isReallocFn(it->first->getCalledFunction()))
            isRealloc = true;
        }

        else if (isCallocLikeFn(it->first, this->tli))
          ArraySizeF = it->first->getArgOperand(1);

        if(ArraySizeF){
          if(isRealloc == 1)
            bitypeUtils->insertUpdate("bitype.Init.", &M, Builder, REALLOC,
                                      (Value *)(it->first), offsets,
                                      bitypeUtils->DL.getTypeAllocSize(it->second),
                                      ArraySizeF, (Value *)(it->first->getArgOperand(0)), NULL);
          else
            bitypeUtils->insertUpdate("bitype.Init.", &M, Builder, HEAPALLOC,
                                      (Value *)(it->first), offsets,
                                      bitypeUtils->DL.getTypeAllocSize(it->second),
                                      ArraySizeF, NULL, NULL);

        }
        }
        //errs() <<M.getName() << " Heap interesting type sum is " << index << "\n";
        //errs() << M.getName() << " Heap added type sum is " << addedIndex << "\n";
    }

    void handleFree(Module &M, std::map<CallInst *, Type *> *heapObjsFree) {
      for(std::map<CallInst *, Type *>::iterator it = heapObjsFree->begin();
          it != heapObjsFree->end(); it++){
            Instruction *next = bitypeUtils->findNextInstruction(it->first);
            IRBuilder<> Builder(next);
            StructElementSetInfoTy offsets;
            std::set<uint32_t> hasVisited;
            bitypeUtils->getArrayAlignment(it->second, offsets, 0, hasVisited);
            bitypeUtils->insertRemove("bitype.Init.", &M, Builder, HEAPALLOC, it->first->getArgOperand(0),
                                      offsets, 0, bitypeUtils->DL.getTypeAllocSize(it->second), NULL);
          }
    }

    void heapObjTracing(Module &M) {
        Instruction *InstPrev;
        this->tli = new TargetLibraryInfo(tlii);
        std::map<CallInst *, Type *> heapObjsFree, heapObjsNew;
        for(Module::iterator F = M.begin(), E = M.end(); F!=E; ++F)
            for(Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB){
                for(BasicBlock::iterator i = BB->begin(), ie = BB->end(); i != ie; i++){
                    if(CallInst *call = dyn_cast<CallInst>(&*i)){
                        collectHeapAlloc(call, &heapObjsNew);
                        collectFree(call, InstPrev, &heapObjsFree);
                    }
                    InstPrev = &*i;
                }

                handleHeapAlloc(M, &heapObjsNew);
                //handleFree(M, &heapObjsFree);

                heapObjsFree.clear();
                heapObjsNew.clear();                
            }
            //errs() << "done the module\n";
        
    }

    bool isHeapObj(CallInst *call) {
      bool isOverloadedNew = false;
      std::string functionName = "";
      if (call->getCalledFunction() != nullptr)
        functionName = call->getCalledFunction()->getName();

      this->tli = new TargetLibraryInfo(tlii);
      int unmangledStatus;
      char *unmangledName =
        abi::__cxa_demangle(functionName.c_str(), nullptr,
                            nullptr, &unmangledStatus);
      if (unmangledStatus == 0) {
        std::string unmangledNameStr(unmangledName);
        if (unmangledNameStr.find("::operator new(unsigned long)") !=
            std::string::npos ||
            unmangledNameStr.find(
              "::operator new(unsigned long, std::nothrow_t const&)") !=
            std::string::npos ||
            unmangledNameStr.find("::operator new[](unsigned long)") !=
            std::string::npos ||
            unmangledNameStr.find(
              "::operator new[](unsigned long, std::nothrow_t const&)")
            != std::string::npos) {
          isOverloadedNew = true;
        }
      }

      if (isAllocCall(call) || isOverloadedNew)
        if (Type *allocTy = getMallocAllocatedType(call, this->tli))
          if (bitypeUtils->isInterestingType(allocTy))
            return true;

      return false;
    }

  bool isLocalPointer(Value *target, Module::iterator F) {
      for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
        for (BasicBlock::iterator t = BB->begin(), te = BB->end();
             t != te; ++t)
          if (target == dyn_cast<AllocaInst>(&*t))
            return true;

      return false;
    }

  bool isSafePointer(Value *target, Module::iterator F, Module *M) {
      if (!isLocalPointer(target, F))
        return false;

      for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
        for (BasicBlock::iterator t = BB->begin(), te = BB->end();
             t != te; ++t)
          if (StoreInst *AI = dyn_cast<StoreInst>(&*t))
            if (target == AI->getPointerOperand())
              if (!isSafeSrcValue(AI->getValueOperand(), F, M))
                return false;

      return true;
    }

    bool isSafeSrcValue(Value *SrcValue, Module::iterator F, Module *M){
      for(Module::global_iterator ii = M->global_begin(); ii != M->global_end(); ++ii){
        if(&*ii == SrcValue)
          return true;
      }

      for(Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
        for(BasicBlock::iterator t = BB->begin(), te = BB->end(); t != te; ++t){
          if(SrcValue == (&*t)){
            if(AllocaInst *AI = dyn_cast<AllocaInst>(&*t)){
              if(bitypeUtils->isInterestingType(AI->getAllocatedType()) && 
              isa<StructType>(AI->getAllocatedType()))
                return true;
              
              else if(AI->getAllocatedType()->isPointerTy())
                if(isSafePointer(AI, F, M))
                  return true;
              return false;
            }

          CallInst *call = dyn_cast<CallInst>(&*t);
          if(call){
            if(isHeapObj(call))
              return true;
            return false;
          }

          // if source type is array (not pointer)
          if(GEPOperator *GO = dyn_cast<GEPOperator>(&*t))
            if(isa<StructType>(GO->getResultElementType()))
              return true;

          // if source type is pointer  
          if(LoadInst *AI = dyn_cast<LoadInst>(&*t))
            if(isSafePointer(AI->getPointerOperand(), F, M))
              return true;
          
          // check store instruction
          if(StoreInst *AI = dyn_cast<StoreInst>(&*t))
            if(isSafeSrcValue(AI->getValueOperand(), F, M))
              return true;
          
          // if source type is related to bitcast
          if(const BitCastInst *BCI = dyn_cast<BitCastInst>(&*t))
            if(isSafeSrcValue(BCI->getOperand(0), F, M))
              return true;
          }
        }
        return false;
    }
    bool compiletime_verification(Value *SrcValue, Module::iterator F, Module *M){
      if(isSafeSrcValue(SrcValue, F, M)){
        return true;
      }
      return false;
    }

    void emitExtendObjTraceInst(Module &M, CallInst *call, int extendTarget){
      ConstantInt *classIndexConst = dyn_cast<ConstantInt>(call->getOperand(1));
      unsigned classIndex = classIndexConst->getZExtValue();
      std::map<unsigned, string>::iterator mapIter = bitypeUtils->bitypeClassIndexSet.find(classIndex);
      if(mapIter != bitypeUtils->bitypeClassIndexSet.end()){
        string className = mapIter->second;
        std::map<string, StructType*>::iterator structIter = bitypeUtils->classStructSet.find(className);
        //llvm::errs() << "get the class name placement in llvm " << className << "\n";
        if(structIter != bitypeUtils->classStructSet.end()){
          StructElementSetInfoTy offsets;
          std::set<uint32_t> hasVisited;
          // std::set<StructType*> tempSet;
          // tempSet.insert(structIter->second);
          // offsets.insert(std::pair<int, std::set<StructType*>>(0, tempSet));
          bitypeUtils->getArrayAlignment(structIter->second, offsets, 0, hasVisited);
          //offsets.insert()
          for(std::map<uint64_t, std::set<StructType*>>::iterator iteraOff = offsets.begin(); iteraOff != offsets.end(); iteraOff++){
            for(std::set<StructType*>::iterator iterSet = iteraOff->second.begin(); iterSet != iteraOff->second.end();){
              StructType* tempStruct = *iterSet;
              if(bitypeUtils->DL.getTypeAllocSize(tempStruct) < 8){
                iteraOff->second.erase(iterSet++);
                llvm::errs() << "delete\n";
              }else{
                iterSet++;
              }
            }
          }

          if(offsets.size() > 0){
            Instruction *next = bitypeUtils->findNextInstruction(call);
            IRBuilder<> Builder(next);
            Value *first = Builder.CreatePtrToInt(call->getOperand(0),
                                            bitypeUtils->IntptrTyN);
            Value *offsetConst = dyn_cast<ConstantInt>(call->getOperand(2));
            Value *NewAddr = Builder.CreateAdd(first, offsetConst);
            Value *ObjAddrT = Builder.CreateIntToPtr(NewAddr, bitypeUtils->IntptrTyN);

            bitypeUtils->insertUpdate("bitype.Init.", &M, Builder, extendTarget, ObjAddrT, offsets,
                                      0, NULL, NULL, NULL);

            //TODO clmakeloginfo
          }
        }
      }
      
    }

    void extendClangInstrumentation(Module &M){
      for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F)
        for (Function::iterator BB = F->begin(), E = F->end(); BB != E;) {
          bool isUpdated = false;
          for (BasicBlock::iterator i = BB->begin(), ie = BB->end();
               i != ie; ++i)
            if (CallInst *call = dyn_cast<CallInst>(&*i))
              if (call->getCalledFunction() != nullptr) {
                std::string FnName = call->getCalledFunction()->getName();
                if (FnName.compare("__placement_new_handle") == 0 ||
                    FnName.compare("__reinterpret_casting_handle") == 0) {
                  // if (bitypeUtils->AllTypeInfo.size() > 0) {
                    if (FnName.compare("__placement_new_handle") == 0)
                      emitExtendObjTraceInst(M, call, PLACEMENTNEW);
                    else if (FnName.compare("__reinterpret_casting_handle") == 0)
                      emitExtendObjTraceInst(M, call, REINTERPRET);
                 // }
                  (&*i)->eraseFromParent();
                  isUpdated = true;
                  break;
                }
              }
          if(isUpdated == false)
            BB++;
        }
    }
    void typecastinginlineoptimization(Module &M){
      GlobalVariable *GLookupStart = bitypeUtils->getLookUpStart(M);
      for(Module::iterator F = M.begin(), E = M.end(); F != E; ++F)
        for(Function::iterator BB = F->begin(), E = F->end(); BB != E;){
          bool update = false;
          for(BasicBlock::iterator i = BB->begin(), ie = BB->end(); i != ie; ++i)
            if(CallInst *call = dyn_cast<CallInst>(&*i)){
              if(call->getCalledFunction() != nullptr){
                std::string functionName = call->getCalledFunction()->getName();
                if(functionName.compare("__bitype_cast_verification") == 0){
                  update = true;
                  Instruction *next = bitypeUtils->findNextInstruction(call);
                  IRBuilder<> Builder(next);

                  // check whether src addr is NULL
                  Value* isNotNull = Builder.CreateIsNotNull(call->getArgOperand(0));
                  Instruction *InsertPtMain = &*Builder.GetInsertPoint();
                  TerminatorInst *ThenTermNotNull, *ElseTermNotNull;
                  //SplitBlockAndInsertIfThenElse(isNotNull, InsertPtMain, &ThenTermNotNull,
                  //                              &ElseTermNotNull, nullptr);
                  ThenTermNotNull = SplitBlockAndInsertIfThen(isNotNull, InsertPtMain, false);
                  // get the first level index
                  Builder.SetInsertPoint(ThenTermNotNull);
                  Value *newPtr = Builder.CreatePtrToInt(call->getArgOperand(1), bitypeUtils->IntptrTyN);
                  Value *firstLeR = Builder.CreateLShr(newPtr, 26);
                  unsigned long l1_mask = (1UL << 22) - 1UL;
                  //ConstantInt *l1_mask_cons = ConstantInt::get(bitypeUtils->Int64Ty, l1_mask, false);
                  Value *firstLeIndex = Builder.CreateAnd(firstLeR, l1_mask);
          
                  // access the first level
                  Value *bitypeLookupInit = Builder.CreateLoad(GLookupStart);
                  Value *firstLeAddr = Builder.CreateGEP(bitypeLookupInit, firstLeIndex, "");

                  firstLeAddr = Builder.CreateLoad(firstLeAddr);

                  Value *isNotNull2 = Builder.CreateIsNotNull(firstLeAddr);

                  Instruction *InsertPt = &*Builder.GetInsertPoint();
                  TerminatorInst *ThenTerm, *ElseTerm;
                  //SplitBlockAndInsertIfThenElse(isNotNull2, InsertPt, &ThenTerm, &ElseTerm, nullptr);
                  ThenTerm = SplitBlockAndInsertIfThen(isNotNull2, InsertPt, false);
                  // access the second level
                  Builder.SetInsertPoint(ThenTerm);

                  Value *secondLeR = Builder.CreateLShr(newPtr, 3);
                  unsigned long l2_mask = (1UL << 23) - 1UL;
                  Value *secondLeIndex = Builder.CreateAnd(secondLeR, l2_mask);

                  secondLeIndex = Builder.CreateShl(secondLeIndex, 1);
                  Value *targetAddr = Builder.CreateGEP(firstLeAddr, secondLeIndex, "");
                  
                  targetAddr = Builder.CreateLoad(targetAddr);

                  Value *isNotNull3 = Builder.CreateIsNotNull(targetAddr);
                  Instruction *InsertPt2 = &*Builder.GetInsertPoint();
                  TerminatorInst *ThenTerm2, *ElseTerm2;
                  ThenTerm2 = SplitBlockAndInsertIfThen(isNotNull3, InsertPt2, false);

                  // get the safe code
                  Builder.SetInsertPoint(ThenTerm2);


                  Value *indexValue = call->getArgOperand(2);

                  Value *targetValue = Builder.CreateGEP(targetAddr, indexValue, "");
                  targetValue = Builder.CreateLoad(targetValue);
                  Value *tempCode = call->getArgOperand(3);

                  // Value *params[] = {targetValue};
                  // Function *debugFunction = (Function*)M.getOrInsertFunction("__bitype_debug_function", 
                  //                   bitypeUtils->VoidTy, bitypeUtils->Int8Ty);
                  // Builder.CreateCall(debugFunction, params);

                  Value *xordValue = Builder.CreateXor(targetValue, tempCode);
                  Value *cmpge = Builder.CreateICmpUGT(xordValue, targetValue);
                  Instruction *InsertPt3 = &*Builder.GetInsertPoint();
                  TerminatorInst *ThenTerm3, *ElseTerm3;
                  //SplitBlockAndInsertIfThenElse(cmpge, InsertPt3, &ThenTerm3, &ElseTerm3, nullptr);
                  ThenTerm3 = SplitBlockAndInsertIfThen(cmpge, InsertPt3, false);

                  // print the type confusion report
                  Builder.SetInsertPoint(ThenTerm3);
                  Value *debugValue = call->getArgOperand(4);
                  Value *param[] = {debugValue};
                  Function *initFunction = (Function*)M.getOrInsertFunction("__bitype_print_type_confusion", 
                                                            bitypeUtils->VoidTy, bitypeUtils->Int32Ty);
                    
                  Builder.CreateCall(initFunction, param);
                  (&*i)->eraseFromParent();
                  break;

                }
              }
            }
            if(update == false)
              BB++; 
        }
    }

    void compileTimeVerification(Module &M){
      for(Module::iterator F = M.begin(), E = M.end(); F != E; F++){
        for(Function::iterator BB = F->begin(), E = F->end(); BB != E;){
          bool isRemoved = false;
          for(BasicBlock::iterator i = BB->begin(), ie = BB->end(); i != ie; ++i){
            if(CallInst *call = dyn_cast<CallInst>(&*i))
              if(call->getCalledFunction() != nullptr){
                std::string functionName = call->getCalledFunction()->getName();
                if(functionName.compare("__bitype_cast_verification") == 0){
                  if(PtrToIntInst *SrcValue = dyn_cast<PtrToIntInst>(call->getArgOperand(0)))
                    if(compiletime_verification(SrcValue->getPointerOperand(), F, &M)){
                      (&*i)->eraseFromParent();
                      isRemoved = true;
                      break;
                    }
                }
              }
          }
          if(isRemoved == false)
            BB++;
        }
      }
    }

    virtual bool runOnModule(Module &M){

        //errs() << "I am on the pass\n";
        BitypeUtil BitypeUtilT(M.getDataLayout());
        bitypeUtils = &BitypeUtilT;

        //Initialization
        bitypeUtils->InitType(M);
        bitypeUtils->InitReadFiles();
        bitypeUtils->getAllClassSafeCast(M, "bitype.Init."); //produce the global variable


        heapObjTracing(M);
        //errs() << "????\n";

        if(ClCompileTimeVerifyOpt)
          compileTimeVerification(M);
        
        if(ClInlineOpt)
          typecastinginlineoptimization(M);
        
        extendClangInstrumentation(M);
        return false;

    }
    };
}

char BitypeInitialize::ID = 0;

INITIALIZE_PASS(BitypeInitialize, "BitypeInitialize",
                        "BitypeInitializePass: bitype initializeiton pass", false, false)

ModulePass *llvm::createBitypeInitializePass(){
    return new BitypeInitialize();
}                        