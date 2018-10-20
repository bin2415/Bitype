//===-- BitypePass.cpp -----------------------------------------------===//
//  Reference to https://github.com/HexHive/HexType
//
// This file is a part of Bitype, a type confusion detector.
//
// The BitypePass has below two functions:
//   - Track Stack object allocation
//   - Track Global object allocation
// This pass will run after all optimization passes run
// The rest is handled by the run-time library.
//===------------------------------------------------------------------===//

#include "llvm/Analysis/CallGraph.h"
#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/BitypeUtil.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/IR/GlobalValue.h"

using namespace llvm;
using std::string;

namespace{
    class Bitype : public ModulePass{
    public:
        static char ID;
        Bitype() : ModulePass(ID) {}

        BitypeUtil *bitypeUtils;
        CallGraph *CG;

        std::set<AllocaInst *> AllAllocaSet;
        std::map<AllocaInst *, IntrinsicInst *> LifeTimeEndMap;
        std::map<AllocaInst *, IntrinsicInst *> LifeTimeStartMap;
        std::map<Function *, std::vector<Instruction *> *> ReturnInstMap;
        std::map<Instruction *, Function *> AllAllocaWithFnMap;
        std::map<Function *, bool> mayCastMap;

    void getAnalysisUsage(AnalysisUsage &Info) const{
        Info.addRequired<CallGraphWrapperPass>();
    }

    Function *setGlobalObjUpdateFn(Module &M){
        FunctionType *VoidFTy = FunctionType::get(Type::getVoidTy(M.getContext()), false);

        Function *FGlobal = Function::Create(VoidFTy, 
                                            GlobalValue::InternalLinkage, "__init_global_object", &M);
        
        FGlobal->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
        FGlobal->addFnAttr(Attribute::NoInline);

        return FGlobal;

    }

        // This is typesan's optimization method
    // to reduce stack object tracing overhead
    bool mayCast(Function *F, std::set<Function*> &visited, bool *isComplete) {
      // Externals may cast
      if (F->isDeclaration())
        return true;

      // Check previously processed
      auto mayCastIterator = mayCastMap.find(F);
      if (mayCastIterator != mayCastMap.end())
        return mayCastIterator->second;

      visited.insert(F);

      bool isCurrentComplete = true;
      for (auto &I : *(*CG)[F]) {
        return true;
        Function *calleeFunction = I.second->getFunction();
        // Default to true to avoid accidental bugs on API changes
        bool result = false;
        // Indirect call
        if (!calleeFunction) {
          result = true;
          // Recursion detected, do not process callee
        } else if (visited.count(calleeFunction)) {
          isCurrentComplete = false;
          // Explicit call to checker
        } else if (
          calleeFunction->getName().find("__bitype_dynamic_cast_verification") !=
          StringRef::npos ||
          calleeFunction->getName().find("__bitype_cast_verification") !=
          StringRef::npos) {
          result = true;
          // Check recursively
        } else {
          bool isCalleeComplete;
          result = mayCast(calleeFunction, visited, &isCalleeComplete);
          // Forbid from caching if callee was not complete (due to recursion)
          isCurrentComplete &= isCalleeComplete;
        }
        // Found a potentialy cast, report true
        if (result) {
          // Cache and report even if it was incomplete
          // Missing traversal can never flip it to not found
          mayCastMap.insert(std::make_pair(F, true));
          *isComplete = true;
          return true;
        }
      }

      // No cast found anywhere, report false
      // Do not cache negative results if current traversal
      // was not complete (due to recursion)
      /*if (isCurrentComplete) {
        mayCastMap.insert(std::make_pair(F, false));
        }*/
      // Report to caller that this traversal was incomplete
      *isComplete = isCurrentComplete;
      return false;
    }

    bool isSafeStackFn(Function *F) {
      assert(F && "Function can't be null");

      std::set<Function*> visitedFunctions;
      bool tmp;
      bool mayCurrentCast = mayCast(&*F, visitedFunctions, &tmp);
      mayCastMap.insert(std::make_pair(&*F, mayCurrentCast));
      if (!mayCurrentCast)
        return false;

      return true;
    }

    void handleFnParameter(Module &M, Function *F) {
      if (F->empty() || F->getEntryBlock().empty() ||
          F->getName().startswith("__init_global_object"))
        return;

      Type *MemcpyParams[] = { bitypeUtils->Int8PtrTy,
        bitypeUtils->Int8PtrTy,
        bitypeUtils->Int64Ty };
      Function *MemcpyFunc =
        Intrinsic::getDeclaration(&M, Intrinsic::memcpy, MemcpyParams);
      for (auto &a : F->args()) {
        Argument *Arg = dyn_cast<Argument>(&a);
        if (!Arg->hasByValAttr())
          return;
        Type *ArgPointedTy = Arg->getType()->getPointerElementType();
        if (bitypeUtils->isInterestingType(ArgPointedTy)) {
          unsigned long size =
            bitypeUtils->DL.getTypeStoreSize(ArgPointedTy);
          IRBuilder<> B(&*(F->getEntryBlock().getFirstInsertionPt()));
          Value *NewAlloca = B.CreateAlloca(ArgPointedTy);
          Arg->replaceAllUsesWith(NewAlloca);
          Value *Src = B.CreatePointerCast(Arg,
                                           bitypeUtils->Int8PtrTy);
          Value *Dst = B.CreatePointerCast(NewAlloca,
                                           bitypeUtils->Int8PtrTy);
          Value *Param[5] = { Dst, Src,
            ConstantInt::get(bitypeUtils->Int64Ty, size),
            ConstantInt::get(bitypeUtils->Int32Ty, 1),
            ConstantInt::get(bitypeUtils->Int1Ty, 0) };
          B.CreateCall(MemcpyFunc, Param);
        }
      }
    }


    void collectLifeTimeInfo(Instruction *I) {
      if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I))
        if ((II->getIntrinsicID() == Intrinsic::lifetime_start)
           || (II->getIntrinsicID() == Intrinsic::lifetime_end)) {
          ConstantInt *Size =
            dyn_cast<ConstantInt>(II->getArgOperand(0));
          if (Size->isMinusOne()) return;

          if (AllocaInst *AI =
             bitypeUtils->findAllocaForValue(II->getArgOperand(1))) {
            if (II->getIntrinsicID() == Intrinsic::lifetime_start)
              LifeTimeStartMap.insert(std::pair<AllocaInst *,
                                   IntrinsicInst *>(AI, II));

            else if (II->getIntrinsicID() == Intrinsic::lifetime_end)
              LifeTimeEndMap.insert(std::pair<AllocaInst *,
                                 IntrinsicInst *>(AI, II));
          }
        }
    }

    void collectAllocaInstInfo(Instruction *I) {
      if (AllocaInst *AI = dyn_cast<AllocaInst>(I))
        if (bitypeUtils->isInterestingType(AI->getAllocatedType())) {
          if (ClSafeStackOpt && bitypeUtils->isSafeStackAlloca(AI))
            return;
          AllAllocaWithFnMap.insert(
            std::pair<Instruction *, Function *>(
              dyn_cast<Instruction>(I), AI->getParent()->getParent()));
          AllAllocaSet.insert(AI);
        }
    }

    void findReturnInst(Function *f){
        std::vector<Instruction *> *TempInstSet = new std::vector<Instruction *>;
        for(inst_iterator j = inst_begin(f), E = inst_end(f); j != E; ++j){
            if(isa<ReturnInst>(&*j))
                TempInstSet->push_back(&*j);
        }
        ReturnInstMap.insert(std::pair<Function *, std::vector<Instruction *>*>(f, TempInstSet));
    }

    void handleAllocaAdd(Module &M){
        int index = 0;
        int addIndex = 0;
        for(AllocaInst *AI : AllAllocaSet){
            Instruction *next = bitypeUtils->findNextInstruction(AI);
            IRBuilder<> Builder(next);

            Value *ArraySizeF = NULL;
            if(ConstantInt *constantSize = dyn_cast<ConstantInt>(AI->getArraySize()))
                ArraySizeF = ConstantInt::get(bitypeUtils->Int64Ty, constantSize->getZExtValue());
            else{
                Value *ArraySize = AI->getArraySize();
                if(ArraySize->getType() != bitypeUtils->Int64Ty)
                    ArraySizeF = Builder.CreateIntCast(ArraySize, bitypeUtils->Int64Ty, false);
                else
                    ArraySizeF = ArraySize;
            }

            Type *AllocaType = AI->getAllocatedType();
            if(bitypeUtils->isInCastRelatedSetType(AllocaType)){
                if(AI->getAlignment() < 8)
                    AI->setAlignment(8);
            }

            StructElementSetInfoTy offsets;
            std::set<uint32_t> hasvisited;
            bitypeUtils->getArrayAlignment(AllocaType, offsets, 0, hasvisited);
            if(bitypeUtils->isInterestingType(AllocaType)){
                 index++;
            }
            if(offsets.size() == 0) continue;
            addIndex++;

            std::map<AllocaInst *, IntrinsicInst *>::iterator LifeTimeStart, LifeTimeEnd;
            LifeTimeStart = LifeTimeStartMap.begin();
            LifeTimeEnd = LifeTimeStartMap.end();

            bool UseLifeTimeInfo = false;
            for(; LifeTimeStart != LifeTimeEnd; LifeTimeStart++){
                if(LifeTimeStart->first == AI){
                    IRBuilder<> BuilderAI(LifeTimeStart->second);
                    bitypeUtils->insertUpdate("bitype", &M, BuilderAI, STACKALLOC, 
                                            AI, offsets, bitypeUtils->DL.getTypeAllocSize(AllocaType),
                                            ArraySizeF, NULL, NULL);
                    UseLifeTimeInfo = true;
                }
            }
            if(UseLifeTimeInfo)
                continue;
                
            bitypeUtils->insertUpdate("bitype", &M, Builder, STACKALLOC, 
                                    AI, offsets, bitypeUtils->DL.getTypeAllocSize(AllocaType),
                                    ArraySizeF, NULL, NULL);
        }
        errs() << M.getName() << " has insteresting type sum is " << index << "\n";
        errs() << M.getName() << " instrumented type sum is " << addIndex << "\n";
    }

    void handleAllocaDelete(Module &M){
        std::map<Instruction *, Function *>::iterator LocalBegin, LocalEnd;
        LocalBegin = AllAllocaWithFnMap.begin();
        LocalEnd = AllAllocaWithFnMap.end();

        for(; LocalBegin != LocalEnd; LocalBegin++){
            Instruction *TargetInst = LocalBegin->first;
            AllocaInst *TargetAlloca = dyn_cast<AllocaInst>(TargetInst);

            Function* TargetFn = LocalBegin->second;

            std::vector<Instruction *> *FnReturnSet;
            FnReturnSet = ReturnInstMap.find(TargetFn)->second;

            std::vector<Instruction *>::iterator ReturnInstCur, ReturnInstEnd;
            ReturnInstCur = FnReturnSet->begin();
            ReturnInstEnd = FnReturnSet->end();
            DominatorTree dt = DominatorTree(*TargetFn);

            bool returnAI = false;
            for(; ReturnInstCur != ReturnInstEnd; ReturnInstCur++)
                if(dt.dominates(TargetInst, *ReturnInstCur)){
                    ReturnInst *returnValue = dyn_cast<ReturnInst>(*ReturnInstCur);
                    if(returnValue->getNumOperands() &&
                        returnValue->getOperand(0) == TargetAlloca){
                            returnAI = true;
                            break;
                        }
                }
            if(returnAI)
                continue;
            
            std::map<AllocaInst *, IntrinsicInst *>::iterator LifeTimeStart, LifeTimeEnd;
            LifeTimeStart = LifeTimeEndMap.begin();
            LifeTimeEnd = LifeTimeEndMap.end();
            bool lifeTimeEndEnable = false;
            for(; LifeTimeStart != LifeTimeEnd; LifeTimeStart++)
                if(LifeTimeStart->first == TargetAlloca){
                    IRBuilder<> BuilderAI(LifeTimeStart->second);
                    bitypeUtils->emitRemoveInst("bitype", &M, BuilderAI, TargetAlloca);
                    lifeTimeEndEnable = true;
                }
            if(lifeTimeEndEnable)
                continue;
            
            ReturnInstCur = FnReturnSet->begin();
            ReturnInstEnd = FnReturnSet->end();

            for(; ReturnInstCur != ReturnInstEnd; ReturnInstCur++)
                if(dt.dominates(TargetInst, *ReturnInstCur)){
                    IRBuilder<> BuilderAI(*ReturnInstCur);
                    bitypeUtils->emitRemoveInst("bitype", &M, BuilderAI, TargetAlloca);
                }
        }
    }

    void stackObjTracing(Module &M){
        for(Module::iterator F = M.begin(), E = M.end(); F != E; ++F){
            if(!bitypeUtils->isInterestingFn(&*F))
                continue;

            if(ClStackOpt && !isSafeStackFn(&*F))
                continue;
            
            handleFnParameter(M, &*F);
            for(Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
                for(BasicBlock::iterator i = BB->begin(), ie = BB->end(); i != ie; ++i){
                    collectLifeTimeInfo(&*i);
                    collectAllocaInstInfo(&*i);
                }

            findReturnInst(&*F);
        }
        handleAllocaAdd(M);
        handleAllocaDelete(M);
    }

    void globalObjTracing(Module &M){
        Function *FGlobal = setGlobalObjUpdateFn(M);
        BasicBlock *BBGlobal = BasicBlock::Create(M.getContext(), "entry", FGlobal);
        IRBuilder<> BuilderGlobal(BBGlobal);
        for(GlobalVariable &GV : M.globals()){
            if(GV.getName() == "llvm.global_ctors" ||
                GV.getName() == "llvm.global_dtors" ||
                GV.getName() == "llvm.global.annotations" ||
                GV.getName() == "llvm.used" || 
                GV.getName().startswith("bitype."))
                    continue;
            
            if(bitypeUtils->isInterestingType(GV.getValueType())){
                StructElementSetInfoTy offsets;
                std::set<uint32_t> hasVisited;
                Value *NElems = NULL;
                Type *AllocaType;

                if(isa<ArrayType>(GV.getValueType())){
                    ArrayType *AI = dyn_cast<ArrayType>(GV.getValueType());
                    AllocaType = AI->getElementType();
                    NElems = ConstantInt::get(bitypeUtils->Int64Ty, AI->getNumElements());
                }else{
                    AllocaType = GV.getValueType();
                    NElems = ConstantInt::get(bitypeUtils->Int64Ty, 1);
                }
                if(bitypeUtils->isInCastRelatedSetType(AllocaType)){
                    if(GV.getAlignment() < 8){
                        GV.setAlignment(8);
                    }
                }


                bitypeUtils->getArrayAlignment(AllocaType, offsets, 0, hasVisited);
                if(offsets.size() == 0) continue;

                bitypeUtils->insertUpdate("bitype", &M, BuilderGlobal, GLOBALALLOC,
                                        &GV, offsets, bitypeUtils->DL.getTypeAllocSize(AllocaType),
                                        NElems, NULL, BBGlobal);
            }
        }
        BuilderGlobal.CreateRetVoid();
        appendToGlobalCtors(M, FGlobal, 0);
    }

    virtual bool runOnModule(Module &M){
        //errs() << "Hello, on the bitype pass\n";
        CG = &getAnalysis<CallGraphWrapperPass>().getCallGraph();
        BitypeUtil bitypeUtilsTmp(M.getDataLayout());
        bitypeUtils = &bitypeUtilsTmp;
        bitypeUtils->InitType(M);

        bitypeUtils->InitReadFiles();
        bitypeUtils->getAllClassSafeCast(M, "bitype"); //produce the global variable

        globalObjTracing(M);

        stackObjTracing(M);

        return false;
    }

};
}

char Bitype::ID = 0;
INITIALIZE_PASS_BEGIN(Bitype, "Bitype",
                    "BitypePass, fast type safety for C++ programs.", false, false)
INITIALIZE_PASS_DEPENDENCY(CallGraphWrapperPass)
INITIALIZE_PASS_END(Bitype, "Bitype",
                    "BitypePass: fast type safety for C++ programs.", false, false)

ModulePass *llvm::createBitypePass(){
    return new Bitype();
}
