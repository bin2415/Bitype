//===- BitypeUtil.cpp - helper functions and classes for Bitype ---------===//
////
////                     The LLVM Compiler Infrastructure
////
//// This file is distributed under the University of Illinois Open Source
//// License. See LICENSE.TXT for details.
////
////  some code reference HexType:https://github.com/HexHive/HexType
////
////===--------------------------------------------------------------------===//

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Transforms/Utils/BitypeUtil.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <fstream>

using std::string;

namespace llvm{
    // extern cl::opt<string> ClCodeMapFile;
    // extern cl::opt<string> ClCastRelatedFile;
    // extern cl::opt<string> ClInheritanceFile;

    // extern cl::opt<string> ClMakeLogInfo;

    cl::opt<string> ClCodeMapFile(
        "bitype-codemap",
        cl::desc("the file path of class code number information"),
        cl::Hidden, cl::init("coding-num.txt"));

    cl::opt<string> ClCastRelatedFile(
        "bitype-castrelated",
        cl::desc("the file path of cast related class set"),
        cl::Hidden, cl::init("castrelated-set.txt"));
    
    cl::opt<string> ClInheritanceFile(
        "bitype-inheritance",
        cl::desc("the file path of class inheritance relationship"),
        cl::Hidden, cl::init("safecast.txt"));
    //ClInheritanceAllFile
     cl::opt<string> ClInheritanceAllFile(
        "bitype-inheritance-all",
        cl::desc("the file path of class inheritance relationship"),
        cl::Hidden, cl::init("bases-set.txt"));

    cl::opt<bool> ClMakeLogInfo(
        "bitype-makelog",
        cl::desc("bitype create log information"),
        cl::Hidden, cl::init(false));
    
    cl::opt<bool> ClStackOpt(
    "stack-opt", cl::desc("stack object optimization (from typesan)"),
    cl::Hidden, cl::init(false));

    cl::opt<bool> ClSafeStackOpt(
    "safestack-opt",
    cl::desc("stack object tracing optimization using safestack"),
    cl::Hidden, cl::init(false));

    cl::opt<bool> ClCompileTimeVerifyOpt(
        "compile-time-verify-opt",
        cl::desc("compile time verification"),
        cl::Hidden, cl::init(false));
    
    cl::opt<bool> ClInlineOpt(
    "inline-opt",
    cl::desc("reduce runtime library function call overhead"),
    cl::Hidden, cl::init(false));

    string BitypeGetConfig::getCodeMapPath(){
        return ClCodeMapFile;
    }
    string BitypeGetConfig::getCastRelatedPath(){
        return ClCastRelatedFile;
    }
    
    int BitypeUtil::readCodeMap(){
        
       
       // ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr = MemoryBuffer::getFile(ClCodeMapFile);
        std::ifstream ifile;
        ifile.open(ClCodeMapFile, std::ios::in);

        assert(ifile.is_open()& 
        "Please configure the class code number informatin file path with -mllvm -bitype-codemap=<file-path>"
        );

        // MemoryBuffer *MB = FileOrErr.get().get();
        // SmallVector<StringRef, 16> lines;
        // SplitString(MB->getBuffer(), lines, "\n\r");

        string temps;
        while(std::getline(ifile, temps)){
            StringRef I(temps);
            // Ignore empty lines
            if(I.empty()){
                continue;
            }
            std::pair<StringRef, StringRef> SplitLine = I.split(',');
            std::pair<string, int> tempCode;
            tempCode.first = SplitLine.first.str();
            int code;
            bool failed = SplitLine.second.getAsInteger(10, code);
            if(failed){
                llvm::errs() << "Bitype read code number file one line is not format\n";
                continue;
            }
            //errs() << "code map " << tempCode.first << "\n";
            tempCode.second = code;
            bitypeCodeMap.insert(tempCode);
        }

        #ifdef BIT_DEBUG
            //llvm::errs() << "Bitype code map size is " << bitypeCodeMap.size() << "\n";
        #endif

        return 1;        
    }

    int BitypeUtil::readRelatedClass(){
        //llvm::ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr = MemoryBuffer::getFile(ClCastRelatedFile);
        std::ifstream ifile;
        ifile.open(ClCastRelatedFile, std::ios::in);
        assert(ifile.is_open() & 
        "Please configure the cast related class informatin file path with -mllvm -bitype-castrelated=<file-path>"
        );

        // MemoryBuffer *MB = FileOrErr.get().get();
        // SmallVector<StringRef, 16> lines;
        // SplitString(MB->getBuffer(), lines, "\n\r");
        string temps;
        unsigned currentIndex = 0;
        //for(auto I = lines.begin(), E=lines.end(); I != E; ++I){
        while(std::getline(ifile, temps)){
            // Ignore empty lines
            StringRef I(temps);
            if(I.empty()){
                continue;
            }
            std::pair<StringRef, StringRef> SplitLine = I.split(',');
            std::pair<string, int> tempCode;
            tempCode.first = SplitLine.first.str();
            int code;
            bool failed = SplitLine.second.getAsInteger(10, code);
            if(failed){
                llvm::errs() << "Bitype read code number file one line is not format\n";
                continue;
            }
            //errs() << "related class " << tempCode.first << "\n";

            tempCode.second = code;
            bitypeCastRelatedClass.insert(tempCode);
            bitypeClassIndexSet.insert(std::pair<unsigned, string>(currentIndex, SplitLine.first.str()));
            bitypeClassIndexSet2.insert(std::pair<string, unsigned>(SplitLine.first.str(), currentIndex));
            currentIndex++;

        }

        #ifdef BIT_DEBUG
            llvm::errs() << "Bitype cast related size is " << bitypeCastRelatedClass.size() << "\n";
        #endif

        return 1;        
    }

    GlobalVariable *BitypeUtil::getLookUpStart(Module &M){
        PointerType *LookupStart = PointerType::get(IntptrTy, 0);
        PointerType *LookupStartP = PointerType::get(LookupStart, 0);
        GlobalVariable *GLookupStart = M.getGlobalVariable("BitypeLookupStart", true);
        if(!GLookupStart){
            GLookupStart = new GlobalVariable(M, LookupStartP, false,
                                                GlobalValue::ExternalLinkage, 0, "BitypeLookupStart");
            GLookupStart->setAlignment(8);
        }
        return GLookupStart;
    }

    GlobalVariable *BitypeUtil::getLookUpSecondLevelGlobal(Module &M, string globalName){
        PointerType *lookupStart = PointerType::get(IntptrTy, 0);
        GlobalVariable *GlookupSecond = M.getGlobalVariable(globalName, true);
        if(!GlookupSecond){
            GlookupSecond = new GlobalVariable(M, lookupStart, false, 
                                            GlobalValue::ExternalLinkage, 0, globalName);
            GlookupSecond->setAlignment(8);
        }
        return GlookupSecond;
    }

    AllocaInst* BitypeUtil::findAllocaForValue(Value *V) {
        if (AllocaInst *AI = dyn_cast<AllocaInst>(V))
        return AI; // TODO: isInterestingAlloca(*AI) ? AI : nullptr;

        AllocaInst *Res = nullptr;
        if (CastInst *CI = dyn_cast<CastInst>(V))
        Res = findAllocaForValue(CI->getOperand(0));
        else if (PHINode *PN = dyn_cast<PHINode>(V))
        for (Value *IncValue : PN->incoming_values()) {
            if (IncValue == PN) continue;
            AllocaInst *IncValueAI = findAllocaForValue(IncValue);
            if (IncValueAI == nullptr || (Res != nullptr && IncValueAI != Res))
            return nullptr;
            Res = IncValueAI;
        }
        return Res;
  }

    bool BitypeUtil::isSafeStackAlloca(AllocaInst *AI) {
    // Go through all uses of this alloca and check whether all accesses to
    // the allocated object are statically known to be memory safe and, hence,
    // the object can be placed on the safe stack.

    SmallPtrSet<const Value *, 16> Visited;
    SmallVector<const Instruction *, 8> WorkList;
    WorkList.push_back(AI);

    // A DFS search through all uses of the alloca in bitcasts/PHI/GEPs/etc.
    while (!WorkList.empty()) {
      const Instruction *V = WorkList.pop_back_val();
      for (const Use &UI : V->uses()) {
        auto I = cast<const Instruction>(UI.getUser());
        assert(V == UI.get());

        switch (I->getOpcode()) {
        case Instruction::Load:
          // Loading from a pointer is safe.
          break;
        case Instruction::VAArg:
          // "va-arg" from a pointer is safe.
          break;
        case Instruction::Store:
          if (V == I->getOperand(0)){
            //NHB odd exception for libc - lets see if it works
            if (I->getOperand(1) == AI &&
               !(AI->getAllocatedType()->isPointerTy()))
              break;
            // Stored the pointer - conservatively assume it may be unsafe.
            return false;
          }
          // Storing to the pointee is safe.
          break;
        case Instruction::GetElementPtr:
          // if (!cast<const GetElementPtrInst>(I)->hasAllConstantIndices())
          // GEP with non-constant indices can lead to memory errors.
          // This also applies to inbounds GEPs, as the inbounds attribute
          // represents an assumption that the address is in bounds,
          // rather than an assertion that it is.
          // return false;

          // We assume that GEP on static alloca with constant indices
          // is safe, otherwise a compiler would detect it and
          // warn during compilation.

          // NHB Todo: this hasn't come up in spec, but it's probably fine
          if (!isa<const ConstantInt>(AI->getArraySize())) {
            // However, if the array size itself is not constant, the access
            // might still be unsafe at runtime.
            return false;
          }
          /* fallthrough */
        case Instruction::BitCast:
        case Instruction::IntToPtr:
        case Instruction::PHI:
        case Instruction::PtrToInt:
        case Instruction::Select:
          // The object can be safe or not, depending on how the result of the
          // instruction is used.
          if (Visited.insert(I).second)
            WorkList.push_back(cast<const Instruction>(I));
          break;

        case Instruction::Call:
        case Instruction::Invoke: {
          // FIXME: add support for memset and memcpy intrinsics.
          ImmutableCallSite CS(I);

          if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
            if (II->getIntrinsicID() == Intrinsic::lifetime_start ||
                II->getIntrinsicID() == Intrinsic::lifetime_end)
              continue;
          }

          // LLVM 'nocapture' attribute is only set for arguments
          // whose address is not stored, passed around, or used in any other
          // non-trivial way.
          // We assume that passing a pointer to an object as a 'nocapture'
          // argument is safe.
          // FIXME: a more precise solution would require an interprocedural
          // analysis here, which would look at all uses of an argument inside
          // the function being called.
          ImmutableCallSite::arg_iterator B = CS.arg_begin(),
            E = CS.arg_end();
          for (ImmutableCallSite::arg_iterator A = B; A != E; ++A)
            /*NHB mod*/
            //!CS.doesNotCapture(A - B))
            if (A->get() == V && V->getType()->isPointerTy()) {
              // The parameter is not marked 'nocapture' - unsafe.
              return false;
            }
          continue;
        }
        default:
          // The object is unsafe if it is used in any other way.
          return false;
        }
      }
    }

    // All uses of the alloca are safe, we can place it on the safe stack.
    return true;
  }

    void BitypeUtil::getDirectTypeInfo(Module &M){
        std::vector<StructType*> Types = M.getIdentifiedStructTypes();
        for(StructType *ST : Types){
            TypeInfo NewType;
            if(!isInterestingStructType(ST))
                continue;
            if(!ST->getName().startswith("trackedtype.") || 
                ST->getName().endswith(".base"))
                continue;
            parsingTypeInfo(ST, NewType, AllTypeNum++);
            AllTypeInfo.push_back(NewType);
        }

        // for(uint32_t i = 0; i < AllTypeNum; i++){
        //     string structName = AllTypeInfo[i].DetailInfo.TypeName;
        //     std::map<string, std::set<string>>::iterator inheriIter = BitypeInheritanceAllSet.find(structName);
        //     if(inheriIter != BitypeInheritanceAllSet.end()){
        //         for(string tempName : inheriIter->second){
        //             TypeDetailInfo AddTypeInfo;
        //             AddTypeInfo.TypeName = tempName;
        //             AddTypeInfo.TypeIndex = 0;
        //             AllTypeInfo[i].DirectParents.push_back(AddTypeInfo);
        //         }
        //     }
        // }
    }

    void BitypeUtil::extendParentSet(int TargetIndex, int CurrentIndex){
        if(VisitCheck[CurrentIndex] == true)
            return;
        
        VisitCheck[CurrentIndex] = true;

        AllTypeInfo[TargetIndex].AllParents.push_back(AllTypeInfo[CurrentIndex].DetailInfo);

        TypeInfo* ParentNode = &AllTypeInfo[CurrentIndex];

        for(uint32_t i = 0; i < ParentNode->DirectParents.size(); i++)
            extendParentSet(TargetIndex, ParentNode->DirectParents[i].TypeIndex);
        
        for(uint32_t i = 0; i < ParentNode->DirectPhantomTypes.size(); i++)
            extendParentSet(TargetIndex, ParentNode->DirectPhantomTypes[i].TypeIndex);
        
        return;
    }

    void BitypeUtil::extendPhantomSet(int TargetIndex, int CurrentIndex){
        if(VisitCheck[CurrentIndex])
            return;
        
        VisitCheck[CurrentIndex] = true;

        AllTypeInfo[TargetIndex].AllPhantomTypes.push_back(AllTypeInfo[CurrentIndex].DetailInfo);

        TypeInfo *ParentNode = &AllTypeInfo[CurrentIndex];

        for(uint32_t i = 0; i < ParentNode->DirectPhantomTypes.size(); i++)
            extendPhantomSet(TargetIndex, ParentNode->DirectPhantomTypes[i].TypeIndex);
        
        return;
    }

    void BitypeUtil::extendTypeRelationInfo(){
        for(u_int32_t i = 0; i < AllTypeNum; i++)
            for(uint32_t j = 0; j < AllTypeInfo[i].DirectParents.size(); j++)
                for(uint32_t t = 0; t < AllTypeNum; t++)
                    if(i != t && (AllTypeInfo[i].DirectParents[j].TypeName == AllTypeInfo[t].DetailInfo.TypeName)){
                        AllTypeInfo[i].DirectParents[j].TypeIndex = t;
                        if(DL.getTypeAllocSize(AllTypeInfo[i].StructTy) == DL.getTypeAllocSize(AllTypeInfo[t].StructTy)){
                            AllTypeInfo[i].DirectPhantomTypes.push_back(AllTypeInfo[t].DetailInfo);
                            AllTypeInfo[t].DirectPhantomTypes.push_back(AllTypeInfo[i].DetailInfo);
                        }
                    }
        
        for(uint32_t i = 0; i < AllTypeNum; i++){
            std::fill_n(VisitCheck, AllTypeNum, false);
            extendParentSet(i, i);
            std::fill_n(VisitCheck, AllTypeNum, false);
            extendPhantomSet(i, i);
        }
    }

    void BitypeUtil::setTypeDetailInfo(StructType *STy, TypeDetailInfo &TargetDetailInfo, uint32_t AllTypeNum){
        string str = STy->getName().str();
        syncTypeName(str);
        TargetDetailInfo.TypeName.assign(str);
        TargetDetailInfo.TypeIndex = AllTypeNum;
        return;
    }
    void BitypeUtil::parsingTypeInfo(StructType *STy, TypeInfo &NewType, uint32_t AllTypeNum){
        NewType.StructTy = STy;
        NewType.ElementSize = STy->elements().size();
        setTypeDetailInfo(STy, NewType.DetailInfo, AllTypeNum);
        if(STy->elements().size() > 0)
            for(StructType::element_iterator I = STy->element_begin(), E = STy->element_end(); I != E; ++I){
                StructType *innerSTy = dyn_cast<StructType>(*I);
                if(innerSTy && isInterestingStructType(innerSTy)){
                    TypeDetailInfo ParentTmp;
                    setTypeDetailInfo(innerSTy, ParentTmp, 0);
                    NewType.DirectParents.push_back(ParentTmp);
                }
            }
        return;
    }

    int BitypeUtil::readInheritanceSet(){
        //llvm::ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr = MemoryBuffer::getFile(ClInheritanceFile);
        std::ifstream ifile;
        ifile.open(ClInheritanceFile, std::ios::in);
        assert(ifstream.is_open() & 
         "Please configure the cast related class informatin file path with -mllvm -bitype-inheritance=<file-path>"
        );

        //std::string ParseError;
        // MemoryBuffer *MB = FileOrErr.get().get();
        // SmallVector<StringRef, 16> lines;
        // SplitString(MB->getBuffer(), lines, "\n\r");

        //for(auto I = lines.begin(), E=lines.end(); I != E; ++I){
        string temps;
        while(std::getline(ifile, temps)){
            StringRef I(temps);
            // Ignore empty lines
            if(I.empty()){
                continue;
            }
            std::set<std::string> tempVec;

            std::pair<StringRef, StringRef> SplitLine = I.split(',');
            
            if(SplitLine.second.empty()){
               continue;
            }
            string currentClass = SplitLine.first.str();

            while(!SplitLine.second.empty()){
                tempVec.insert(SplitLine.first.str());
                StringRef temp = SplitLine.second;
                SplitLine = temp.split(',');
            }

            if(!SplitLine.first.empty()){
                tempVec.insert(SplitLine.first.str());
            }

            BitypeInheritanceSet.insert(std::pair<string, std::set<string>>(currentClass, tempVec));
            
            #ifdef BIT_DEBUG
            //llvm::errs() << "Class " << currentClass << " has " << tempVec.size() << " bases\n";
            #endif
        }

         return 1;   
}

  int BitypeUtil::readInheritanceAllSet(){
        //llvm::ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr = MemoryBuffer::getFile(ClInheritanceFile);
        std::ifstream ifile;
        ifile.open(ClInheritanceAllFile, std::ios::in);
        assert(ifstream.is_open() & 
         "Please configure the cast related class informatin file path with -mllvm -bitype-inheritance=<file-path>"
        );

        //std::string ParseError;
        // MemoryBuffer *MB = FileOrErr.get().get();
        // SmallVector<StringRef, 16> lines;
        // SplitString(MB->getBuffer(), lines, "\n\r");

        //for(auto I = lines.begin(), E=lines.end(); I != E; ++I){
        string temps;
        while(std::getline(ifile, temps)){
            StringRef I(temps);
            // Ignore empty lines
            if(I.empty()){
                continue;
            }
            std::set<std::string> tempVec;

            std::pair<StringRef, StringRef> SplitLine = I.split(',');
            
            if(SplitLine.second.empty()){
               continue;
            }
            string currentClass = SplitLine.first.str();

            while(!SplitLine.second.empty()){
                tempVec.insert(SplitLine.first.str());
                StringRef temp = SplitLine.second;
                SplitLine = temp.split(',');
            }

            if(!SplitLine.first.empty()){
                tempVec.insert(SplitLine.first.str());
            }

            BitypeInheritanceAllSet.insert(std::pair<string, std::set<string>>(currentClass, tempVec));
            
            #ifdef BIT_DEBUG
            //llvm::errs() << "Class " << currentClass << " has " << tempVec.size() << " bases\n";
            #endif
        }

         return 1;   
}

   void removeTargetStr(string &FullStr, string RemoveStr){
        string::size_type i;
        while((i = FullStr.find(RemoveStr)) != string::npos){   
            if(RemoveStr.compare("::") == 0)
                FullStr.erase(FullStr.begin(), FullStr.begin() + i + 2);
            else
                FullStr.erase(i, RemoveStr.length());
        }
    }

    void removeTargetNum(string& TargetStr){
        string::size_type i;
        if((i = TargetStr.find(".")) == string::npos)
            return;
        
        if((i + 1 < TargetStr.size()) && 
            (TargetStr[i+1] >= '0' && TargetStr[i+1] <= '9')){
                TargetStr.erase(i, TargetStr.size() - i);
        }
    }
    
    void BitypeUtil::syncTypeName(string &TargetStr){
        SmallVector<string, 12> RemoveStrs;
        RemoveStrs.push_back("::");
        RemoveStrs.push_back("class.");
        RemoveStrs.push_back("./");
        RemoveStrs.push_back("struct.");
        RemoveStrs.push_back("union.");
        RemoveStrs.push_back(".base");
        RemoveStrs.push_back("trackedtype.");
        RemoveStrs.push_back("blacklistedtype.");
        RemoveStrs.push_back("*");
        RemoveStrs.push_back("'");

        for(unsigned long i = 0; i < RemoveStrs.size(); i++)
            removeTargetStr(TargetStr, RemoveStrs[i]);
        removeTargetNum(TargetStr);
    }

    void BitypeUtil::syncModuleName(string& TargetStr){
        SmallVector<string, 12> RemoveStrs;
        RemoveStrs.push_back("./");
        RemoveStrs.push_back(".");
        RemoveStrs.push_back("/");

        for(unsigned long i = 0; i < RemoveStrs.size(); i++)
            removeTargetStr(TargetStr, RemoveStrs[i]);

        removeTargetNum(TargetStr);        
    }

 

    bool BitypeUtil::isInterestingStructType(StructType *STy) {
        if (STy->isStructTy() &&
            STy->hasName() &&
            !STy->isLiteral() &&
            !STy->isOpaque())
        return true;

        return false;
  }

  bool BitypeUtil::isInCastRelatedSetStructType(StructType *STy){
      return isInCastRelatedSet(STy);
  }

    bool BitypeUtil::isInterestingArrayType(ArrayType *ATy) {
        Type *InnerTy = ATy->getElementType();

        if (StructType *InnerSTy = dyn_cast<StructType>(InnerTy))
            return isInterestingStructType(InnerSTy);

        if (ArrayType *InnerATy = dyn_cast<ArrayType>(InnerTy))
            return isInterestingArrayType(InnerATy);

        return false;
  }

  bool BitypeUtil::isInCastRelatedSetArrayType(ArrayType *ATy){
      Type *InnerTy = ATy->getElementType();
      if(StructType *InnerSty = dyn_cast<StructType>(InnerTy))
        return isInCastRelatedSetStructType(InnerSty);
      
      if(ArrayType *InnerATy = dyn_cast<ArrayType>(InnerTy))
        return isInCastRelatedSetArrayType(InnerATy);
    
      return false;
  }

    bool BitypeUtil::isInterestingType(Type *rootType){
        if(StructType *Sty = dyn_cast<StructType>(rootType))
            return isInterestingStructType(Sty);
        
        if(ArrayType *Aty = dyn_cast<ArrayType>(rootType))
            return isInterestingArrayType(Aty);
        
        return false;
    }

    bool BitypeUtil::isInCastRelatedSetType(Type *rootType){
        if(StructType *STy = dyn_cast<StructType>(rootType))
            return isInCastRelatedSetStructType(STy);
        
        if(ArrayType *Aty = dyn_cast<ArrayType>(rootType))
            return isInCastRelatedSetArrayType(Aty);
        
        return false;
    }



    void BitypeUtil::InitReadFiles(){
        readCodeMap();
        readRelatedClass();
        readInheritanceSet();
        //readInheritanceAllSet();
    }

    void BitypeUtil::getOffsetZeroArray(Type *AI, StructOffsetZeroSet &zeroSet){
        if(ArrayType *Array = dyn_cast<ArrayType>(AI)){
            Type *AllocaType = Array->getElementType();
            getOffsetZeroArray(AllocaType, zeroSet);            
        }else if(StructType *STy = dyn_cast<StructType>(AI)){
            if(isInterestingStructType(STy)){
                getOffetZeroStruct(STy, zeroSet);
            }else{
                return;
            }
        }        
    }

    void BitypeUtil::getOffetZeroStruct(StructType *STy, StructOffsetZeroSet &zeroSet){

        const StructLayout *SL = DL.getStructLayout(STy);
        if(STy->getName().startswith("trackedtype.")){
                zeroSet.insert(STy);
        }

        for(unsigned i = 0, e = STy->getNumElements(); i < e; i++){
            uint32_t temp = SL->getElementOffset(i);
            if(temp != 0)
                continue;    
            Type* Ty = STy->getElementType(i);
            getOffsetZeroArray(Ty, zeroSet);
        }
    }


    void BitypeUtil::getAllClassSafeCast(Module &M, string Prefix){
        getDirectTypeInfo(M);
        if(AllTypeInfo.size() > 0)
            extendTypeRelationInfo();

        std::vector<StructType*> Types = M.getIdentifiedStructTypes();
        for(StructType *ST : Types){
            if(!BitypeUtil::isInterestingStructType(ST))
                continue;

            if(!ST->getName().startswith("trackedtype.") || 
            ST->getName().endswith(".base"))
                continue;

            StructOffsetZeroSet zeroSet;
            string stName = ST->getName().str();
            BitypeUtil::syncTypeName(stName);
            
            // #ifdef BIT_DEBUG
            // llvm::errs() << "struct name is " << stName << ",";
            // #endif
            
            std::map<string, int>::iterator familyIter = bitypeCastRelatedClass.find(stName);
            if(familyIter == bitypeCastRelatedClass.end()){
               
                // #ifdef BIT_DEBUG
                // llvm::errs() << "what a shame, it is not cast related class, we do not care it\n";
                // #endif

                continue;
            }
            classStructSet.insert(std::pair<string, StructType*>(stName, ST));
            getOffetZeroStruct(ST, zeroSet);
            std::set<int> safeNum;

            std::map<string, int>::iterator codeIter = bitypeCodeMap.find(stName);

            int currentClassFamilySize = familyIter->second;

            if(codeIter != bitypeCodeMap.end()){
                int currentClassCode = codeIter->second;

                // #ifdef BIT_DEBUG
                // llvm::errs() << "its code is " << currentClassCode << "\n";
                // #endif

                safeNum.insert(currentClassCode);
            }

            for(StructType *subSTy : zeroSet){
                string subName = subSTy->getName().str();        
                BitypeUtil::syncTypeName(subName);
                codeIter = bitypeCodeMap.find(subName);
                familyIter = bitypeCastRelatedClass.find(subName);
                
                // struct have the code number and in the cast related class set
                if(codeIter != bitypeCodeMap.end() && familyIter != bitypeCastRelatedClass.end()){
                    int subClassCode = codeIter->second;

                    if(currentClassFamilySize < familyIter->second){
                        currentClassFamilySize = familyIter->second;               
                    }      

                    if(safeNum.find(subClassCode) == safeNum.end()){
                        safeNum.insert(subClassCode);
                    }
                }

                std::map<string, std::set<string>>::iterator inheritanceSet = BitypeInheritanceSet.find(subName);
                if(inheritanceSet != BitypeInheritanceSet.end()){
                    for(string parentName : inheritanceSet->second){
                        codeIter = bitypeCodeMap.find(parentName);
                        //errs() << "parent name is " << parentName << "\n";
                        familyIter = bitypeCastRelatedClass.find(parentName);
                        if(codeIter != bitypeCodeMap.end() && familyIter != bitypeCastRelatedClass.end()){
                            int parentClassCode = codeIter->second;
                            if(safeNum.find(parentClassCode) == safeNum.end()){
                                //errs() << "parent code is " << parentClassCode << "\n";
                                safeNum.insert(parentClassCode);
                            }
                        }
                    }
                }//end if(inheritanceSet != BitypeInheritanceSet.end())
            }

            extendSafeNum(safeNum, stName, currentClassFamilySize);

            std::vector<unsigned char> tempCodingResult;

            int arraySize = currentClassFamilySize / 8;
            if(currentClassFamilySize % 8 != 0)
                arraySize += 1;

            if(safeNum.size() > 0)
                BitypeUtil::computeCode(safeNum, tempCodingResult, arraySize);
            else{
                for(int i = 0; i < arraySize; i++){
                    unsigned char tempChar = (unsigned char)0;
                    tempCodingResult.push_back(tempChar);
                }
            }

           
            std::vector<Constant *> vectorInit;

            //added debug
            std::map<string, unsigned>::iterator tempIter2 = bitypeClassIndexSet2.find(stName);
            if(tempIter2 == bitypeClassIndexSet2.end()){
                llvm::errs() << "sorry, can't find the class index : " << stName << "\n";
                continue;
            }
            
            unsigned tempIndex = tempIter2->second;
            // llvm::errs() << "class " << stName << " : " << tempIndex << "\n";
            unsigned tempMask = (1 << 8) - 1;
            //llvm::errs() << "class " << stName << " : " << (tempMask & tempMask) << "\n";
            unsigned char firstByte = (unsigned char)(tempIndex & tempMask);
            unsigned char secondByte = (unsigned char)((tempIndex >> 8)&tempMask);
            //added debug
            vectorInit.push_back(ConstantInt::get(Int8Ty, firstByte));
            vectorInit.push_back(ConstantInt::get(Int8Ty, secondByte));
            for(unsigned char tempCoding : tempCodingResult){
                vectorInit.push_back(ConstantInt::get(Int8Ty, tempCoding));
            }
            

            // Emit the global information variable
            string mname = M.getName();
            BitypeUtil::syncModuleName(mname);

            string tempGlobalName = "";

            getGlobalName(Prefix, mname, stName, tempGlobalName);
            
            ArrayType* arrayType = ArrayType::get(Int8Ty, arraySize + 2);
            Constant* arrayInit = ConstantArray::get(arrayType, vectorInit);

            GlobalVariable * tempGlobalVariable = new GlobalVariable(
                M, arrayType, true,/*isConstant*/
                GlobalVariable::LinkageTypes::PrivateLinkage,
                arrayInit/*Initializer*/, tempGlobalName);
            
            tempGlobalVariable->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);

            if(GlobalVariableMap.find(stName) == GlobalVariableMap.end()){
                // #ifdef BIT_DEBUG
                // llvm::errs() << "Add global variable " << tempGlobalName << "\n";
                // #endif

                GlobalVariableMap.insert(std::pair<string, GlobalVariable *>(stName, tempGlobalVariable));
            }


        }
    }

    void BitypeUtil::extendSafeNum(std::set<int> &safeNum, const string stName, int& currentFamilySize){
        if(AllTypeInfo.size() == 0)
            return;
        
        std::map<string, int>::iterator codeIter;
        std::map<string, int>::iterator familyIter;
        for(uint32_t i = 0; i < AllTypeInfo.size(); i++){
            if(AllTypeInfo[i].DetailInfo.TypeName == stName){
                for(auto tempDetailInfo : AllTypeInfo[i].AllParents){
                    string tempStName = tempDetailInfo.TypeName;
                    codeIter = bitypeCodeMap.find(tempStName);
                    if(codeIter != bitypeCodeMap.end()){
                        familyIter = bitypeCastRelatedClass.find(tempStName);
                        if(familyIter != bitypeCastRelatedClass.end()){
                            int tempFamilySize = familyIter->second;
                            if(currentFamilySize < tempFamilySize)
                                currentFamilySize = tempFamilySize;
                        }
                        int tempCode = codeIter->second;
                        if(safeNum.find(tempCode) == safeNum.end()){
                            safeNum.insert(tempCode);
                        }
                    } //end if(codeIter != bitypeCodeMap.end())
                } // end for(auto tempDetailInfo : AllTypeInfo[i].AllParents)

                for(auto tempDetailInfo : AllTypeInfo[i].AllPhantomTypes){
                    string tempStName = tempDetailInfo.TypeName;
                    codeIter = bitypeCodeMap.find(tempStName);
                    if(codeIter != bitypeCodeMap.end()){
                        familyIter = bitypeCastRelatedClass.find(tempStName);
                        if(familyIter != bitypeCastRelatedClass.end()){
                            int tempFamilySize = familyIter->second;
                            if(currentFamilySize < tempFamilySize)
                                currentFamilySize = tempFamilySize;
                        }
                        int tempCode = codeIter->second;
                        if(safeNum.find(tempCode) == safeNum.end()){
                            safeNum.insert(tempCode);
                        }
                    }
                }
                break;
            }
        }
        return;
    }

    GlobalVariable* BitypeUtil::CreateGlobalVariable(string prefix, string mname, Module &M, std::set<StructType*> relatedCast){
          StructElementSetInfoTy setAlignments;
          std::set<uint32_t> hasVisited;
          int maxFamilySize = 0;
          GlobalVariable * tempGlobalVariable2 = nullptr;
          if(relatedCast.size() > 0){
                //llvm::errs() << "Hello, the alignment 8 is on\n";
                std::set<int> safeNum;
                StructOffsetZeroSet zeroSet;
                std::map<string, int>::iterator familyIter;
                for(StructType* tempStruct : relatedCast){
                    string tempStName = tempStruct->getName().str();
                    syncTypeName(tempStName);
                    std::map<string, int>::iterator codeIter2 = bitypeCodeMap.find(tempStName);
                    getOffetZeroStruct(tempStruct, zeroSet);
                    familyIter = bitypeCastRelatedClass.find(tempStName);
                    int currentClassFamilySize2 = familyIter->second;
                    if(maxFamilySize < currentClassFamilySize2)
                        maxFamilySize = currentClassFamilySize2;

                    if(codeIter2 != bitypeCodeMap.end()){
                        int currentClassCode = codeIter2->second;
                        if(safeNum.find(currentClassCode) == safeNum.end())
                            safeNum.insert(currentClassCode);
                    }
                }//end for(StructType *)

            for(StructType *subSTy : zeroSet){
                string subName = subSTy->getName().str();        
                BitypeUtil::syncTypeName(subName);
                std::map<string, int>::iterator codeIter = bitypeCodeMap.find(subName);
                familyIter = bitypeCastRelatedClass.find(subName);
                
                // struct have the code number and in the cast related class set
                if(codeIter != bitypeCodeMap.end()){
                    int subClassCode = codeIter->second;
                    if(safeNum.find(subClassCode) == safeNum.end()){
                        safeNum.insert(subClassCode);
                    }
                }

                std::map<string, std::set<string>>::iterator inheritanceSet2 = BitypeInheritanceSet.find(subName);
                if(inheritanceSet2 != BitypeInheritanceSet.end()){
                    for(string parentName : inheritanceSet2->second){
                        codeIter = bitypeCodeMap.find(parentName);
                        //errs() << "parent name is " << parentName << "\n";
                        familyIter = bitypeCastRelatedClass.find(parentName);
                        if(codeIter != bitypeCodeMap.end() && familyIter != bitypeCastRelatedClass.end()){
                            int parentClassCode = codeIter->second;
                            if(safeNum.find(parentClassCode) == safeNum.end()){
                                //errs() << "parent code is " << parentClassCode << "\n";
                                safeNum.insert(parentClassCode);
                            }
                        }
                    }
                }//end if(inheritanceSet != BitypeInheritanceSet.end())
            }//end for(StructType *subSTy)


            std::vector<unsigned char> tempCodingResult2;

            int arraySize2 = maxFamilySize / 8;
            if(maxFamilySize % 8 != 0)
                arraySize2 += 1;

            if(safeNum.size() > 0)
                BitypeUtil::computeCode(safeNum, tempCodingResult2, arraySize2);
            else{
                for(int i = 0; i < arraySize2; i++){
                    unsigned char tempChar = (unsigned char)0;
                    tempCodingResult2.push_back(tempChar);
                }
            }

           
            std::vector<Constant *> vectorInit;
            vectorInit.push_back(ConstantInt::get(Int8Ty, 0));
            vectorInit.push_back(ConstantInt::get(Int8Ty, 0));
            for(unsigned char tempCoding : tempCodingResult2){
                vectorInit.push_back(ConstantInt::get(Int8Ty, tempCoding));
            }

            string setResultName = "";
            getGlobalNameSet(prefix, mname, relatedCast, setResultName);
            string tmpGlobalName = "";
            for(StructType* tempSt : relatedCast){
                string stnametmp = tempSt->getName().str();
                syncTypeName(stnametmp);
                tmpGlobalName += stnametmp;
            }
            llvm::errs() << "Global Name is " << setResultName << "\n";
            if(GlobalVariableMap.find(tmpGlobalName) == GlobalVariableMap.end()){
                ArrayType* arrayType2 = ArrayType::get(Int8Ty, arraySize2 + 2);
                Constant* arrayInit2 = ConstantArray::get(arrayType2, vectorInit);

                tempGlobalVariable2 = new GlobalVariable(
                    M, arrayType2, true,/*isConstant*/
                    GlobalVariable::LinkageTypes::PrivateLinkage,
                    arrayInit2/*Initializer*/, setResultName);
            
                tempGlobalVariable2->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
                GlobalVariableMap.insert(std::pair<string, GlobalVariable*>(tmpGlobalName, tempGlobalVariable2));
            }
          
    }
          return tempGlobalVariable2;
    }

    void BitypeUtil::getGlobalNameSet(string prefix, string mname, std::set<StructType*> setSty, string &resultName){
        resultName += prefix;
        resultName += mname;
        for(StructType* sty: setSty){
            string styName = sty->getName().str();
            syncTypeName(styName);
            resultName += ".";
            resultName += styName;
        }
    }

    bool BitypeUtil::isInterestingFn(Function *F){
        if(F->empty() || F->getEntryBlock().empty() ||
            F->getName().startswith("__init_global_object"))
            return false;
        return true;
    }

    void BitypeUtil::getGlobalName(string prefix, string mname, string sname, string &resultName){
        resultName += prefix;
        resultName += mname;
        resultName += ".";
        resultName += sname;
    }

    void BitypeUtil::computeCode(std::set<int> setNum, std::vector<unsigned char> &codingResult, int familySize){
        std::map<int, unsigned char> indexedCode;
        for(int tempIndex : setNum){
            //errs() << "temp index is " << tempIndex << "\n";
            tempIndex--;
            int setIndex = tempIndex / 8;
            int offset = tempIndex % 8;
            unsigned char tempCode = 1 << offset;
            std::map<int, unsigned char>::iterator indexIter = indexedCode.find(setIndex);
            if(indexIter != indexedCode.end()){
                indexIter->second = indexIter->second ^ tempCode;
            }else{
                indexedCode.insert(std::pair<int, unsigned char>(setIndex, tempCode));
            }           
        }

        unsigned char tempChar = (unsigned char)0; 
        for(int i = 0; i < familySize; i++){
            std::map<int, unsigned char>::iterator indexIter = indexedCode.find(i);
            if(indexIter != indexedCode.end()){
                codingResult.push_back(indexIter->second);
            }else{
                codingResult.push_back(tempChar);
            }
        }
        
    }
    
    void BitypeUtil::emitRemoveInst(string prefix, Module *SrcM, IRBuilder<> &Builder, AllocaInst *TargetAlloca){
        Value *ArraySize = NULL;
        if(ConstantInt *constantSize = dyn_cast<ConstantInt>(TargetAlloca->getArraySize()))
            ArraySize = ConstantInt::get(Int64Ty, constantSize->getZExtValue());
        else{
            Value *arraySize = TargetAlloca->getArraySize();
            if(arraySize->getType() != Int64Ty)
                ArraySize = Builder.CreateIntCast(arraySize, Int64Ty, false);
            else
                ArraySize = arraySize;
        }
        StructElementSetInfoTy Elements;
        std::set<uint32_t> hasVisited;
        Type *AllocaType = TargetAlloca->getAllocatedType();
        getArrayAlignment(AllocaType, Elements, 0, hasVisited);
        if(Elements.size() == 0) return;
        insertRemove(prefix, SrcM, Builder, STACKALLOC, TargetAlloca,
                    Elements, ArraySize, DL.getTypeAllocSize(AllocaType), NULL);
    }

    void BitypeUtil::InitType(Module &M){
        LLVMContext& Ctx = M.getContext();

        VoidTy = Type::getVoidTy(Ctx);
        Int8PtrTy = PointerType::getUnqual(Type::getInt8Ty(Ctx));
        Int32PtrTy = PointerType::getUnqual(Type::getInt32Ty(Ctx));
        Int64PtrTy = PointerType::getUnqual(Type::getInt64Ty(Ctx));
        IntptrTy = Type::getInt8PtrTy(Ctx);
        IntptrTyN = DL.getIntPtrType(Ctx); //Returns an integer type with size at least as bit as that of a pointer in the given address space.
        //Int128Ty = Type::getInt128Ty(Ctx);
        Int64Ty = Type::getInt64Ty(Ctx);
        Int32Ty = Type::getInt32Ty(Ctx);
        Int8Ty = Type::getInt8Ty(Ctx);
        Int1Ty = Type::getInt1Ty(Ctx);
        
    }

    Instruction* BitypeUtil::findNextInstruction(Instruction *CurInst){
        BasicBlock::iterator it(CurInst);
        ++it;
        if(it == CurInst->getParent()->end())
            return NULL;
        
        return &*it;
    }

    void BitypeUtil::getStructAlignment(StructType *STy, StructElementSetInfoTy &Elements, uint32_t Offset, std::set<uint32_t>& hasVisited){
        const StructLayout *SL = DL.getStructLayout(STy);
        bool duplicate = false;
        if(hasVisited.find(Offset) != hasVisited.end())
            duplicate = true;
        if(!duplicate){
            if(STy->getName().startswith("trackedtype.") && isInCastRelatedSet(STy)){
                int alignment = Offset / 8;
                hasVisited.insert(Offset);
                if(Elements.find(alignment) == Elements.end()){
                    std::set<StructType*> tempSet;
                    tempSet.insert(STy);
                    Elements.insert(std::pair<uint64_t, std::set<StructType*>>(alignment, tempSet));
                }else{
                    StructElementSetInfoTy::iterator tempIter = Elements.find(alignment);
                    tempIter->second.insert(STy);
                }
            }
        }
        for(unsigned i = 0, e = STy->getNumElements(); i != e; i++){
            uint32_t tmp = SL->getElementOffset(i) + Offset;
            getArrayAlignment(STy->getElementType(i), Elements, tmp, hasVisited);
        }
    }

    void BitypeUtil::getArrayAlignment(Type* Ty, StructElementSetInfoTy &Elements, uint32_t Offset, std::set<uint32_t>& hasVisited){
           if(ArrayType *Array = dyn_cast<ArrayType>(Ty)){
            uint32_t ArraySize = Array->getNumElements();
            Type *AllocaType = Array->getElementType();
            for(uint32_t i = 0; i < ArraySize; i++){
                getArrayAlignment(AllocaType, Elements, (Offset + (i * DL.getTypeAllocSize(AllocaType))), hasVisited);
            }
        }

        else if(StructType *STy = dyn_cast<StructType>(Ty)){
            if(isInterestingStructType(STy))
                getStructAlignment(STy, Elements, Offset, hasVisited);
        }
    }

    void BitypeUtil::getStructOffsets(StructType *STy, StructElementInfoTy &Elements, uint32_t Offset){
        const StructLayout *SL = DL.getStructLayout(STy);
        bool duplicate = false;
        //uint32_t tempOffset = Offset / 8;
        if(Elements.find(Offset) != Elements.end())
            duplicate = true;
        
        if(!duplicate){
            if(STy->getName().startswith("trackedtype.") && isInCastRelatedSet(STy))
            {
                
                 Elements.insert(std::pair<uint64_t, StructType*>(Offset, STy));
                 //llvm::errs() << "Insert the struct in offsets " << STy->getName() << "\n";
            }
               
        }

        for(unsigned i = 0, e = STy->getNumElements(); i != e; i++){
            uint32_t tmp = SL->getElementOffset(i) + Offset;
            getArrayOffsets(STy->getElementType(i), Elements, tmp);
        }
    }
    
    void BitypeUtil::getArrayOffsets(Type *Ty, StructElementInfoTy &Elements, uint32_t Offset){
        if(ArrayType *Array = dyn_cast<ArrayType>(Ty)){
            uint32_t ArraySize = Array->getNumElements();
            Type *AllocaType = Array->getElementType();
            for(uint32_t i = 0; i < ArraySize; i++){
                getArrayOffsets(AllocaType, Elements, (Offset + (i * DL.getTypeAllocSize(AllocaType))));
            }
        }

        else if(StructType *STy = dyn_cast<StructType>(Ty)){
            if(isInterestingStructType(STy))
                getStructOffsets(STy, Elements, Offset);
        }
    }

    bool BitypeUtil::isInCastRelatedSet(StructType *STy){
        string styname = STy->getName().str();
        BitypeUtil::syncTypeName(styname);
        if(bitypeCastRelatedClass.find(styname) != bitypeCastRelatedClass.end())
            return true;
        //errs() << styname << " doesn't in the cast related set\n";
        return false;
    }

    void BitypeUtil::insertUpdate(string prefix, Module *SrcM, IRBuilder<> &Builder, uint32_t AllocType,
                                Value *ObjAddr, StructElementSetInfoTy &Elements, 
                                uint32_t TypeSize, Value *ArraySize,
                                Value *ReallocAddr, BasicBlock *BB){

        if(AllocType == REALLOC)
            emitInstForObjTrace(prefix, SrcM, Builder, Elements, VLAOBJADD, ObjAddr,
                                ArraySize, TypeSize, 0, AllocType, ReallocAddr, BB);
        else if(AllocType == PLACEMENTNEW || AllocType == REINTERPRET)
            emitInstForObjTrace(prefix, SrcM, Builder, Elements, CONOBJADD, ObjAddr,
                                ArraySize, TypeSize, 0, AllocType, NULL, BB);
        else if(dyn_cast<ConstantInt>(ArraySize) && AllocType != HEAPALLOC){
            ConstantInt *constantSize = dyn_cast<ConstantInt>(ArraySize);
            for(uint32_t i = 0; i < constantSize->getZExtValue(); i++){
                emitInstForObjTrace(prefix, SrcM, Builder, Elements, CONOBJADD,
                                    ObjAddr, ArraySize, TypeSize, i, AllocType, NULL, BB);
            }
        }
        else
            emitInstForObjTrace(prefix, SrcM, Builder, Elements, VLAOBJADD, ObjAddr, ArraySize,
                                TypeSize, 0, AllocType, NULL, BB);
    }

    void BitypeUtil::emitInstForObjTrace(string prefix, Module *SrcM, IRBuilder<> &Builder, StructElementSetInfoTy &Elements,
                                        uint32_t EmitType, Value *ObjAddr, Value *ArraySize, uint32_t TypeSizeInt,
                                        uint32_t CurrArrayIndex, uint32_t AllocType, Value *ReallocAddr, BasicBlock* BB){
        

        if(ObjAddr && ObjAddr->getType()->isPtrOrPtrVectorTy())
            ObjAddr = Builder.CreatePointerCast(ObjAddr, Int64PtrTy);

        if(ReallocAddr && ReallocAddr->getType()->isPtrOrPtrVectorTy())
            ReallocAddr = Builder.CreatePointerCast(ReallocAddr, Int64PtrTy);

        
        //type size
        Value *TypeSize = ConstantInt::get(Int32Ty, TypeSizeInt);
        ConstantInt *constantTypeSize = dyn_cast<ConstantInt>(TypeSize);
        string mname = SrcM->getName().str();
        BitypeUtil::syncModuleName(mname);

        bool isFirstEntry = true;
        int elementSize = Elements.size();
        int currentIndex = 0;
        for(auto &entry : Elements){
            currentIndex++;
            uint32_t OffsetInt;

            OffsetInt = entry.first;

            OffsetInt = OffsetInt * 8;
            
            Value *OffsetV = ConstantInt::get(Int32Ty, OffsetInt);
            OffsetInt += (constantTypeSize->getZExtValue() * CurrArrayIndex);
            Value *first = ConstantInt::get(IntptrTyN, OffsetInt);
            Value *second = Builder.CreatePtrToInt(ObjAddr, IntptrTyN);
            Value *NewAddr = Builder.CreateAdd(first, second);
            Value *ObjAddrT = Builder.CreateIntToPtr(NewAddr, IntptrTyN);

            Value *Constant8 = ConstantInt::get(Int32Ty, 8);

            Value *AllocTypeV = ConstantInt::get(Int32Ty, AllocType);

            std::set<StructType *> currentStructSet = entry.second;
            string stName = "";

            if(currentStructSet.size() == 0)
                continue;

            for(StructType* tempSty : currentStructSet){
                string tmpStName = tempSty->getName().str();
                syncTypeName(tmpStName);
                stName += tmpStName;
            }
            std::map<string, GlobalVariable*>::iterator gvmIter = GlobalVariableMap.find(stName);
            
           
            // string stName = currentStruct->getName();
            // syncTypeName(stName);

            // string tempGlobalName = "";

            // getGlobalName(prefix, mname, stName, tempGlobalName);

            // GlobalVariable *currentGlobalVari = SrcM->getGlobalVariable(tempGlobalName, true);
            // if(!currentGlobalVari){
                
            //     #ifdef BIT_DEBUG
            //     llvm::errs() << "the struct " << tempGlobalName << " doesn't exist in global variable map\n";
            //     #endif
            //     continue;
            // }
            // errs() << "Get the global variable" << "\n";

            //GlobalVariable *currentGlobalVari = gvmIter->second; //Get the global variable that store the safe code
            //Value* tempglobalVariable = Builder.CreateBitCast(currentGlobalVari, IntptrTy);
            //errs() << "Get the global variable\n";
            GlobalVariable *currentGlobalVari;
            if(gvmIter == GlobalVariableMap.end()){
                errs() << "Can't get the global struct " << stName << "\n";
                currentGlobalVari = CreateGlobalVariable(prefix, mname, *SrcM, currentStructSet);
            }else{
                currentGlobalVari = gvmIter->second;
            }

            if(currentGlobalVari == nullptr)
                continue;        
            
            switch(EmitType){
                case CONOBJADD:{
                    if(ClInlineOpt && AllocType != REINTERPRET &&
                        AllocType != GLOBALALLOC){
                            GlobalVariable* GlookUpStart = getLookUpStart(*SrcM);
                            GlobalVariable *GlookUpSecondStart = getLookUpSecondLevelGlobal(*SrcM, "BitypeSecondLevelStart");
                            GlobalVariable *GlookUpSecondEnd = getLookUpSecondLevelGlobal(*SrcM, "BitypeSecondLevelEnd");
                            //Value *addrInt = Builder.CreatePtrToInt(ObjAddrT, IntptrTyN);
                            //llvm::errs() << "Hello, get in\n";
                            Value *idx11 = Builder.CreateLShr(NewAddr, 26);
                            unsigned long l1_mask = (1UL << 22) - 1UL;
                            Value *idx1 = Builder.CreateAnd(idx11, l1_mask);
                            unsigned long l2_mask = (1UL << 23) - 1UL;
                            Value *idx22 = Builder.CreateLShr(NewAddr, 3);
                            Value *idx2 = Builder.CreateAnd(idx22, l2_mask);

                            idx2 = Builder.CreateShl(idx2, 1);
                            Value* idx222 = Builder.CreateAdd(idx2, ConstantInt::get(Int64Ty, 1));


                            Value *bitypeLookupInit = Builder.CreateLoad(GlookUpStart);
                            Value *firstLevelValue1 = Builder.CreateGEP(bitypeLookupInit, idx1);
                            Value *firstLevelValue = Builder.CreateLoad(firstLevelValue1);

                            //if the term is null
                            Value *isNull = Builder.CreateIsNull(firstLevelValue);
                            Instruction *InsertPt = &*Builder.GetInsertPoint();
                            TerminatorInst *ThenTerm, *ElseTerm;
                            SplitBlockAndInsertIfThenElse(isNull, InsertPt, 
                                                        &ThenTerm, &ElseTerm, nullptr);
                            Builder.SetInsertPoint(ThenTerm);
                            Value *bitypeLookupSecondStart = Builder.CreateLoad(GlookUpSecondStart);
                            Value *bitypeLookupSecondEnd = Builder.CreateLoad(GlookUpSecondEnd);
                            
                            // // if the BitypeSecondLevelStart >= BitypeSecondLevelEnd
                            Value *bitypeSecondGE = Builder.CreateICmpUGE(bitypeLookupSecondStart, bitypeLookupSecondEnd);
                            Instruction *InsertPt2 = &*Builder.GetInsertPoint();
                            TerminatorInst *ThenTerm2;
                            ThenTerm2 = SplitBlockAndInsertIfThen(bitypeSecondGE, InsertPt2, false);
                            Builder.SetInsertPoint(ThenTerm2);
                            
                            Function *initFunction = (Function*)SrcM->getOrInsertFunction("__bitype_malloc_new_space", 
                                                            VoidTy);
                            Builder.CreateCall(initFunction);

                            Builder.SetInsertPoint(InsertPt2);
                            
                            bitypeLookupSecondStart = Builder.CreateLoad(GlookUpSecondStart);
                            Value *storedAddressed = Builder.CreateGEP(bitypeLookupInit, idx1);
                            Builder.CreateStore(bitypeLookupSecondStart, storedAddressed);
                            
                            // // level2[idx2] = address
                            // unsigned long l2_mask = (1UL << 23) - 1UL;
                            // Value *idx2 = Builder.CreateLShr(addrInt, 3);
                            // idx2 = Builder.CreateAnd(idx2, l2_mask);
                            Value *storedAddr = Builder.CreateGEP(bitypeLookupSecondStart, idx2);
                            Value* currentGlobalVariTemp2 = Builder.CreatePointerCast(currentGlobalVari, Int8PtrTy);

                            
                            Builder.CreateStore(currentGlobalVariTemp2, storedAddr);

                            Value* loadedAddr4 = Builder.CreateGEP(bitypeLookupSecondStart, idx222);
                            Value* arrayOffset2 = Builder.CreateBitCast(loadedAddr4, PointerType::get((Type*)StructType::get(Int32Ty, Int32Ty), 0));
                            
                           
                            //arrayOffset2->getType()->dump();
                            //arrayOffset2 = Builder.CreateLoad(arrayOffset2);
                            
                            //llvm::errs() << "Hello?\n";
                            std::vector<Value*> tempVec;
                            tempVec.push_back((Value*)ConstantInt::get(Int32Ty, 0));
                            tempVec.push_back((Value*)ConstantInt::get(Int32Ty, 0));
                            
                            ArrayRef<Value*> tempArr = ArrayRef<Value*>(tempVec);
                            //arrayOffset2->getType()->dump();
                            Value* size2 = Builder.CreateGEP(arrayOffset2, tempArr);
                            //llvm::errs() << "Hello\n";
                            //size2->getType()->dump();
                            Builder.CreateStore(ConstantInt::get(Int32Ty, 1), size2);
                            //llvm::errs() << "Hello End\n";

                            tempVec.pop_back();
                            tempVec.push_back((Value*)ConstantInt::get(Int32Ty, 1));
                            ArrayRef<Value*> tempArr2 = ArrayRef<Value*>(tempVec);
                            Value* offset2 = Builder.CreateGEP(arrayOffset2, tempArr2);
                            Builder.CreateStore(OffsetV, offset2);
                        

                            // // bitypeLookupSecondStart = bitypeLookupSecondStart + 1UL << 23
                            unsigned long addedAddr = 1UL << 24;
                            Value *addedAddrValue = ConstantInt::get(Int64Ty, addedAddr);
                            Value *loadedAddr = Builder.CreateGEP(bitypeLookupSecondStart, addedAddrValue);
                            //Value *bitypeSecondStart2 = Builder.CreateLoad(GlookUpSecondStart);
                            
                            //loadedAddr->getType()->dump();
                            //cast<PointerType>(GlookUpSecondStart->getType())->getElementType()->dump();
                            Builder.CreateStore(loadedAddr, GlookUpSecondStart);

                            Builder.SetInsertPoint(ElseTerm);
                            Value* loadedAddr2 = Builder.CreateGEP(firstLevelValue, idx2);
                            Builder.CreateStore(currentGlobalVariTemp2, loadedAddr2);

                            //size_offset* tmp_soff = (size_offset*)&(level2[idx22]);
                            //tmp_soff->size = (uint32_t)ArraySize;
                            //tmp_soff->offset = offset;
                            //Value* idx222 = Builder.CreateAdd(idx2, ConstantInt::get(Int32Ty, 1));
                            Value* loadedAddr3 = Builder.CreateGEP(firstLevelValue, idx222);
                            
                            Value* arrayOffset = Builder.CreateBitCast(loadedAddr3, PointerType::get((Type*)StructType::get(Int32Ty, Int32Ty), 0));
                            Value* size = Builder.CreateGEP(arrayOffset, tempArr);
                            Builder.CreateStore(ConstantInt::get(Int32Ty, 0), size);
                            Value* offset = Builder.CreateGEP(arrayOffset, tempArr2);
                            Builder.CreateStore(OffsetV, offset);
                            Builder.SetInsertPoint(InsertPt);
                            //llvm::errs() << "Hello, there ?\n";
                        }
                    else{ 
                        string funName = "";
                        if(AllocType == REINTERPRET)
                            funName += "__bitype_handle_reinterpret_cast";
                        else
                            funName += "__bitype_direct_updateObjTrace";
                        
                        Value *Param[3] = {ObjAddrT, currentGlobalVari, OffsetV};
                        Function *initFunction = (Function*)SrcM->getOrInsertFunction(funName, VoidTy,
                                                IntptrTyN, IntptrTyN, Int32Ty);
                        
                        Builder.CreateCall(initFunction, Param);
                        } //end else

                    if (ClMakeLogInfo) {
                    Value *AllocTypeV =
                    ConstantInt::get(Int32Ty, AllocType);
                    Function *ObjUpdateFunction =
                    (Function*)SrcM->getOrInsertFunction(
                        "__obj_update_count", VoidTy,
                        Int32Ty, Int64Ty);
                    Value *TmpOne = ConstantInt::get(Int64Ty, 1);
                    Value *Param[2] = {AllocTypeV, TmpOne};
                    Builder.CreateCall(ObjUpdateFunction, Param);
                    }
                    break;
                }
                case VLAOBJADD:{
                    
                    //TODO Inline opt
                    // if(ClInlineOpt){

                    //         GlobalVariable* GlookUpStart = getLookUpStart(*SrcM);
                    //         GlobalVariable *GlookUpSecondStart = getLookUpSecondLevelGlobal(*SrcM, "BitypeSecondLevelStart");
                    //         GlobalVariable *GlookUpSecondEnd = getLookUpSecondLevelGlobal(*SrcM, "BitypeSecondLevelEnd");
                    //         //Value *addrInt = Builder.CreatePtrToInt(ObjAddrT, IntptrTyN);
                    //         //llvm::errs() << "Hello, get in\n";

                    //         Value* index_i = Builder.CreateAlloca(Int64Ty);
                    //         Builder.CreateStore(ConstantInt::get(Int64Ty, 0), index_i);

                    //         BasicBlock* curBB = &*Builder.GetInsertBlock();
                    //         Instruction* curInst = &*Builder.GetInsertPoint(); 
                    //         BasicBlock* condationBlock = SplitBlock(curBB, curInst);

                    //         Builder.SetInsertPoint(curInst);
                    //         Value* temp_index_i = Builder.CreateLoad(index_i);
                    //         Value* forCon = Builder.CreateICmpULT(temp_index_i, ArraySize);
                    //         TerminatorInst* thenTermMain;

                    //         Instruction* mainPt = &*Builder.GetInsertPoint();
                    //         thenTermMain = SplitBlockAndInsertIfThen(forCon, mainPt, false);

                    //         Builder.SetInsertPoint(thenTermMain);

                    //         temp_index_i = Builder.CreateLoad(index_i);
                    //         Value *tempVal = Builder.CreateMul(TypeSize, temp_index_i);
                    //         temp_index_i = Builder.CreateAdd(temp_index_i, ConstantInt::get(Int64Ty, 1));
                    //         Builder.CreateStore(temp_index_i, index_i);
                    //         Value *curAddr = Builder.CreateAdd(NewAddr, tempVal);

                    //         Value *idx11 = Builder.CreateLShr(curAddr, 26);
                    //         unsigned long l1_mask = (1UL << 22) - 1UL;
                    //         Value *idx1 = Builder.CreateAnd(idx11, l1_mask);
                    //         unsigned long l2_mask = (1UL << 23) - 1UL;
                    //         Value *idx22 = Builder.CreateLShr(curAddr, 3);
                    //         Value *idx2 = Builder.CreateAnd(idx22, l2_mask);

                    //         idx2 = Builder.CreateShl(idx2, 1);
                    //         Value* idx222 = Builder.CreateAdd(idx2, ConstantInt::get(Int64Ty, 1));


                    //         Value *bitypeLookupInit = Builder.CreateLoad(GlookUpStart);
                    //         Value *firstLevelValue1 = Builder.CreateGEP(bitypeLookupInit, idx1);
                    //         Value *firstLevelValue = Builder.CreateLoad(firstLevelValue1);

                    //         //if the term is null
                    //         Value *isNull = Builder.CreateIsNull(firstLevelValue);
                    //         Instruction *InsertPt = &*Builder.GetInsertPoint();
                    //         TerminatorInst *ThenTerm, *ElseTerm;
                    //         SplitBlockAndInsertIfThenElse(isNull, InsertPt, 
                    //                                     &ThenTerm, &ElseTerm, nullptr);
                    //         Builder.SetInsertPoint(ThenTerm);
                    //         Value *bitypeLookupSecondStart = Builder.CreateLoad(GlookUpSecondStart);
                    //         Value *bitypeLookupSecondEnd = Builder.CreateLoad(GlookUpSecondEnd);
                            
                    //         // // if the BitypeSecondLevelStart >= BitypeSecondLevelEnd
                    //         Value *bitypeSecondGE = Builder.CreateICmpUGE(bitypeLookupSecondStart, bitypeLookupSecondEnd);
                    //         Instruction *InsertPt2 = &*Builder.GetInsertPoint();
                    //         TerminatorInst *ThenTerm2;
                    //         ThenTerm2 = SplitBlockAndInsertIfThen(bitypeSecondGE, InsertPt2, false);
                    //         Builder.SetInsertPoint(ThenTerm2);
                            
                    //         Function *initFunction = (Function*)SrcM->getOrInsertFunction("__bitype_malloc_new_space", 
                    //                                         VoidTy);
                    //         Builder.CreateCall(initFunction);

                    //         Builder.SetInsertPoint(InsertPt2);
                            
                    //         bitypeLookupSecondStart = Builder.CreateLoad(GlookUpSecondStart);
                    //         Value *storedAddressed = Builder.CreateGEP(bitypeLookupInit, idx1);
                    //         Builder.CreateStore(bitypeLookupSecondStart, storedAddressed);
                            
                    //         // // level2[idx2] = address
                    //         // unsigned long l2_mask = (1UL << 23) - 1UL;
                    //         // Value *idx2 = Builder.CreateLShr(addrInt, 3);
                    //         // idx2 = Builder.CreateAnd(idx2, l2_mask);
                    //         Value *storedAddr = Builder.CreateGEP(bitypeLookupSecondStart, idx2);
                    //         Value* currentGlobalVariTemp2 = Builder.CreatePointerCast(currentGlobalVari, Int8PtrTy);

                            
                    //         Builder.CreateStore(currentGlobalVariTemp2, storedAddr);

                    //         Value* loadedAddr4 = Builder.CreateGEP(bitypeLookupSecondStart, idx222);
                    //         Value* arrayOffset2 = Builder.CreateBitCast(loadedAddr4, PointerType::get((Type*)StructType::get(Int32Ty, Int32Ty), 0));
                            
                           
                    //         //arrayOffset2->getType()->dump();
                    //         //arrayOffset2 = Builder.CreateLoad(arrayOffset2);
                            
                    //         //llvm::errs() << "Hello?\n";
                    //         std::vector<Value*> tempVec;
                    //         tempVec.push_back((Value*)ConstantInt::get(Int32Ty, 0));
                    //         tempVec.push_back((Value*)ConstantInt::get(Int32Ty, 0));
                            
                    //         ArrayRef<Value*> tempArr = ArrayRef<Value*>(tempVec);
                    //         //arrayOffset2->getType()->dump();
                    //         Value* size2 = Builder.CreateGEP(arrayOffset2, tempArr);
                    //         //llvm::errs() << "Hello\n";
                    //         //size2->getType()->dump();
                    //         Builder.CreateStore(ConstantInt::get(Int32Ty, 1), size2);
                    //         //llvm::errs() << "Hello End\n";

                    //         tempVec.pop_back();
                    //         tempVec.push_back((Value*)ConstantInt::get(Int32Ty, 1));
                    //         ArrayRef<Value*> tempArr2 = ArrayRef<Value*>(tempVec);
                    //         Value* offset2 = Builder.CreateGEP(arrayOffset2, tempArr2);
                    //         Builder.CreateStore(OffsetV, offset2);
                        

                    //         // // bitypeLookupSecondStart = bitypeLookupSecondStart + 1UL << 23
                    //         unsigned long addedAddr = 1UL << 24;
                    //         Value *addedAddrValue = ConstantInt::get(Int64Ty, addedAddr);
                    //         Value *loadedAddr = Builder.CreateGEP(bitypeLookupSecondStart, addedAddrValue);
                    //         //Value *bitypeSecondStart2 = Builder.CreateLoad(GlookUpSecondStart);
                            
                    //         //loadedAddr->getType()->dump();
                    //         //cast<PointerType>(GlookUpSecondStart->getType())->getElementType()->dump();
                    //         Builder.CreateStore(loadedAddr, GlookUpSecondStart);

                    //         Builder.SetInsertPoint(ElseTerm);
                    //         Value* loadedAddr2 = Builder.CreateGEP(firstLevelValue, idx2);
                    //         Builder.CreateStore(currentGlobalVariTemp2, loadedAddr2);

                    //         //size_offset* tmp_soff = (size_offset*)&(level2[idx22]);
                    //         //tmp_soff->size = (uint32_t)ArraySize;
                    //         //tmp_soff->offset = offset;
                    //         //Value* idx222 = Builder.CreateAdd(idx2, ConstantInt::get(Int32Ty, 1));
                    //         Value* loadedAddr3 = Builder.CreateGEP(firstLevelValue, idx222);
                            
                    //         Value* arrayOffset = Builder.CreateBitCast(loadedAddr3, PointerType::get((Type*)StructType::get(Int32Ty, Int32Ty), 0));
                    //         Value* size = Builder.CreateGEP(arrayOffset, tempArr);
                    //         Builder.CreateStore(ConstantInt::get(Int32Ty, 0), size);
                    //         Value* offset = Builder.CreateGEP(arrayOffset, tempArr2);
                    //         Builder.CreateStore(OffsetV, offset);
                    //         Builder.SetInsertPoint(InsertPt);
                            
                    //         Builder.CreateBr(condationBlock);

                    //         Builder.SetInsertPoint(mainPt); 
                               
                    // }else{
                    Function *initFunction = (Function*)SrcM->getOrInsertFunction("__bitype_updateObjTrace", 
                    VoidTy, IntptrTyN, IntptrTyN, Int32Ty, Int32Ty, Int32Ty);
                    Value *Param[5] = {ObjAddrT, currentGlobalVari, TypeSize, ArraySize, OffsetV};
                    Builder.CreateCall(initFunction, Param);
                    // }

                    if(isFirstEntry){
                        //Value *arraySizeAddr = Builder.CreateIntToPtr(Builder.CreateSub(second, Constant8), IntptrTyN);
                        Value *Param[2] = {ObjAddr, ArraySize};
                        Function *updateArraySizeFunc = (Function *)SrcM->getOrInsertFunction("__bitype_update_arraySize", VoidTy, IntptrTyN, Int32Ty);
                        Builder.CreateCall(updateArraySizeFunc, Param);
                    }

                    if (ClMakeLogInfo) {
                        Value *AllocTypeV =
                        ConstantInt::get(Int32Ty, AllocType);
                        Function *ObjUpdateFunction =
                        (Function*)SrcM->getOrInsertFunction(
                            "__obj_update_count", VoidTy,
                            Int32Ty, Int64Ty);
                        //Value *TmpOne = ConstantInt::get(Int64Ty, ArraySize);
                        Value *Param[2] = {AllocTypeV, ArraySize};
                        Builder.CreateCall(ObjUpdateFunction, Param);
                    }
                    break;
                }
                case CONOBJDEL:{

                    if(ClInlineOpt){
                        GlobalVariable* GlookUpStart = getLookUpStart(*SrcM);
                        Value *idx11 = Builder.CreateLShr(NewAddr, 26);
                        unsigned long l1_mask = (1UL << 22) - 1UL;
                        Value *idx1 = Builder.CreateAnd(idx11, l1_mask);
                        unsigned long l2_mask = (1UL << 23) - 1UL;
                        Value *idx22 = Builder.CreateLShr(NewAddr, 3);
                        Value *idx2 = Builder.CreateAnd(idx22, l2_mask);
                        idx2 = Builder.CreateShl(idx2, 1);
                        Value *bitypeLookupInit = Builder.CreateLoad(GlookUpStart);
                        Value *firstLevelValue1 = Builder.CreateGEP(bitypeLookupInit, idx1);
                        Value *firstLevelValue = Builder.CreateLoad(firstLevelValue1);

                        Value* isnotNull = Builder.CreateIsNotNull(firstLevelValue);

                        Instruction* InsertPt = &*Builder.GetInsertPoint();
                        TerminatorInst *ThenTerm;
                        ThenTerm = SplitBlockAndInsertIfThen(isnotNull, InsertPt, false);
                        Builder.SetInsertPoint(ThenTerm);
                        //Then Branch
                        Value *nullpointer = ConstantPointerNull::get((PointerType*)Int8PtrTy);

                        Value *gepvalue = Builder.CreateGEP(firstLevelValue, idx2);
                        Builder.CreateStore(nullpointer, gepvalue);
                        Value *size_offset = Builder.CreateGEP(firstLevelValue, 
                                                                Builder.CreateAdd(idx2, ConstantInt::get(Int64Ty, 1)));
                        Builder.CreateStore(nullpointer, size_offset);
                        // end Then branch
                        
                        Builder.SetInsertPoint(InsertPt);
                    }
                    else{
                    Function *initFunction = (Function *)SrcM->getOrInsertFunction("__bitype_direct_eraseObj", VoidTy, IntptrTyN);
                    Value *Param[1] = {ObjAddrT};
                    Builder.CreateCall(initFunction, Param);
                    }
                    // llvm::errs() << "wrong aaaa end\n";
                    break;
                }
                case VLAOBJDEL:{
                    Function *initFunction = (Function *)SrcM->getOrInsertFunction("__bitype_eraseObj", VoidTy, IntptrTyN, IntptrTyN, Int32Ty,
                                                                                Int64Ty, Int32Ty);
                    //Value *arraySizeAddr = Builder.CreateIntToPtr(Builder.CreateSub(second, Constant8), IntptrTyN);
                    Value *Param[5] = {ObjAddrT, ObjAddr, TypeSize, ArraySize, AllocTypeV};
                    Builder.CreateCall(initFunction, Param);

                    //erase the array size
                    if(currentIndex == elementSize){
                        Function *initFunction = (Function *)SrcM->getOrInsertFunction("__bitype_direct_eraseArraySize", VoidTy, IntptrTyN);
                        Value *Param[1] = {ObjAddr};
                        Builder.CreateCall(initFunction, Param);
                    }

                    break;
                }
                default:
                    break;

            }

        }                                              
    }

    void BitypeUtil::insertRemove(string prefix, Module *SrcM, IRBuilder<> &Builder, uint32_t AllocType,
                                    Value *ObjAddr, StructElementSetInfoTy &Elements, Value *ArraySize,
                                    int TypeSize, BasicBlock *BB){
        if(Elements.size() == 0)
            return;
        
        if(ArraySize == NULL)
            ArraySize = ConstantInt::get(Int64Ty, 1);
        
        if(AllocType != HEAPALLOC && dyn_cast<ConstantInt>(ArraySize)){
            ConstantInt *constantSize = dyn_cast<ConstantInt>(ArraySize);
            for(uint32_t i = 0; i < constantSize->getZExtValue(); i++){
                emitInstForObjTrace(prefix, SrcM, Builder, Elements, CONOBJDEL,
                                    ObjAddr, ArraySize, TypeSize, i, AllocType, NULL, NULL);
            }
        }else{
            emitInstForObjTrace(prefix, SrcM, Builder, Elements, VLAOBJDEL, ObjAddr, ArraySize,
                                TypeSize, 0, AllocType, NULL, NULL);
        }
                                    }

    
}