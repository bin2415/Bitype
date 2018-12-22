#include "clang/AST/ASTConsumer.h"
#include "clang/Sema/SemaConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"
#include "clang/AST/RecordLayout.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Driver/Options.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "clang/CodeGen/CodeGenABITypes.h"

#include <string>
#include <map>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <set>
#include <stack>
#include <unistd.h>
#include <fstream>

using namespace clang;
using namespace clang::tooling;
using namespace std;

#define MAX_NUM_CLASS 35000

static QualType NullQualType;

// struct to store the information

//struct to store the bases classes and phantom classes
// struct safeset{
//    set<string> bases;
//    set<string> phantoms;
// }; 

struct codeStruct{
    int index;
    int sum;
};

// struct classInfo{
//     int classIndex;
//     unsigned classSize;
// };

map<string, vector<string>> basesVec;

typedef SmallVector<const CXXRecordDecl *, 64> BaseVec;
map<string, int> classCode;
map<int, string> numberName;
int currentIndex = 0;
map<string, vector<string>> relations;
set<string> castClass;
set<string> castrelatedClass;
set<string> hasVisitedField;
set<string> hasVisitedBases;
set<string> downCastVec;
map<string, int> classSize;
set<string> downCastLoc;
set<string> basedCastClass;

bool** graph;

bool** graph_phantoms;

bool** graph_family;
// reference to caver


// struct store the information end
class FindNamedClassVisitor
  : public RecursiveASTVisitor<FindNamedClassVisitor> {
public:
  explicit FindNamedClassVisitor(ASTContext *Context)
    : Context(Context) {}


  static void CollectAllBases(const CXXRecordDecl *BaseRD, BaseVec &bases){
      SmallVector<const CXXRecordDecl*, 8> Queue;
      const CXXRecordDecl *Record = BaseRD;
      //llvm::errs() << "hello, get in " << BaseRD->getName() << "\n";;
      while(true){
          for(const auto &I : Record->bases()){
              const RecordType *Ty = I.getType()->getAs<RecordType>();
              if(!Ty){
                  continue;
              }
              //llvm::errs() << "its base is " << Ty->getTypeClassName() << "\n";
              CXXRecordDecl *Base = cast_or_null<CXXRecordDecl>(Ty->getDecl()->getDefinition());
              if(!Base)
                continue;
            //llvm::errs() << "its base is " << Base->getName() << "\n";
            Queue.push_back(Base);
            bases.push_back(Base);
          }
          if(Queue.empty())
            break;
          Record = Queue.pop_back_val();
      }

  }

  bool VisitCXXRecordDecl(CXXRecordDecl *Declaration) {
    // if(!Declaration->isThisDeclarationADefinition())
    //     return true;

    if(Declaration && Declaration->hasDefinition() && !Declaration->isAnonymousStructOrUnion()){
    
    if(!Declaration->getDeclName()){
      return true;
    }
    string declName = Declaration->getName().str();
    //llvm::errs() << "hello, class " << declName << "\n";
    // if(declName == "nsBaseHashtableET"){
    //     llvm::errs() << "oh, hoo\n";
    //     exit(1);
    // }
    //llvm::errs() << "current class is " << declName << " : ";
	// need to determine the CXXRecordDecl is undependent context
    // othersize, the context->getASTRecordLayout(Declaration) will segamentation fault.
    // Dependent class can't be found.
    int declIndex = 0;
    if(hasVisitedField.find(declName) == hasVisitedField.end() && !Declaration->isDependentContext()){ // have not get the field
      hasVisitedField.insert(declName);
	 
      //llvm::errs() << "Before getASTRecordLayout" << Declaration->getName() << "\n";
      const ASTRecordLayout &typeLayout = Context->getASTRecordLayout(Declaration);
      CharUnits cxxAlignment  = typeLayout.getAlignment();
      classSize.insert(pair<string, int>(declName, (int)cxxAlignment.getQuantity()));
      
	  //Find the offset 0 struct
      if(!Declaration->isAggregate() && !Declaration->isPolymorphic()){
        for(const auto *FD: Declaration->fields()){

            //reference https://github.com/sslab-gatech/caver
            //llvm::errs() << "Excited" << "\n";
            // Ignore an anoymous field.
            if(!FD->getDeclName()){
                continue;
            }// end if(!FD->getDeclName())
            
            QualType QT = FD->getType().getUnqualifiedType();
            bool isCompundElem = false;
            QualType ElemQT = getElemQualTypeOrNull(QT, isCompundElem);
            if(ElemQT == NullQualType || !isCompundElem){
                continue;
            }
            uint64_t fieldOffsetBits = typeLayout.getFieldOffset(FD->getFieldIndex());
            if(fieldOffsetBits == 0){ // add it to the relationship
                int fieldIndex = 0;
                map<string, int>::iterator declIter = classCode.find(declName);
                if(declIter == classCode.end()){
                    declIndex = currentIndex;
                    numberName.insert(pair<int, string>(currentIndex, declName));
                    classCode.insert(pair<string, int>(declName, currentIndex));
                    currentIndex++;
                    if(currentIndex > MAX_NUM_CLASS){
                        llvm::errs() << "shame, class index is big than " << MAX_NUM_CLASS << "\n";
                        exit(-1);
                    }
                }else{
                    declIndex = declIter->second;
                }
                string fieldName = FD->getName().str();
                declIter = classCode.find(fieldName);
                if(declIter == classCode.end()){
                    fieldIndex = currentIndex;
                    numberName.insert(pair<int, string>(currentIndex, fieldName));
                    classCode.insert(pair<string, int>(fieldName, currentIndex));
                    currentIndex++;
                    if(currentIndex > MAX_NUM_CLASS){
                        llvm::errs() << "shame, class index is big than " << MAX_NUM_CLASS << "\n";
                        exit(-1);
                    }
                }else{
                    fieldIndex = declIter->second;
                }// end get index

                //llvm::errs() << "Combination offset 0" << Declaration->getName() << " : " <<  FD->getName() << "\n";
                graph[declIndex][fieldIndex] = true; 
                graph[fieldIndex][declIndex] = true;
                }
        }
    }
    }
    
      //collect the all family classes
    //   if(Declaration->isDependentContext()){
    //       return true;
    //   }
	    declIndex = 0;
      //if(hasVisitedBases.find(declName) == hasVisitedBases.end()){
         
         // hasVisitedBases.insert(declName);
          map<string, int>::iterator declIter = classCode.find(declName);
          if(declIter == classCode.end()){
            declIndex = currentIndex;
            numberName.insert(pair<int, string>(currentIndex, declName));
            classCode.insert(pair<string, int>(declName, currentIndex));
            currentIndex++;
          }else{
            declIndex = declIter->second;
          }
          bool isDependent = true;
          CharUnits leafSize;
          if(!Declaration->isDependentContext()){
              isDependent = false;
              const ASTRecordLayout &typeLayout = Context->getASTRecordLayout(Declaration);
              leafSize = typeLayout.getSize();
          }
          
        vector<string> tempBaseVec;
        //llvm::errs() << "hello\n";
        
        BaseVec Bases;
        CollectAllBases(Declaration, Bases);
        for(const auto Base: Bases){
            string baseName = Base->getName();
            // if(declName == "nsBaseHashtableET"){
            //     llvm::errs() << "nsBaseHashtableET base : " << baseName << "\n";
            //     exit(1);
            // }

            if(baseName == "")
                continue;
            tempBaseVec.push_back(baseName);
            int baseIndex = 0;
            declIter = classCode.find(baseName);
            if(declIter != classCode.end()){
                baseIndex = declIter->second;
            }else{
                baseIndex = currentIndex;
                numberName.insert(pair<int, string>(currentIndex, baseName));
                classCode.insert(pair<string, int>(baseName, currentIndex));
                currentIndex++;
            }
            graph[baseIndex][declIndex] = true;
            graph[declIndex][baseIndex] = true;
            graph_family[baseIndex][declIndex] = true;
            graph_family[declIndex][baseIndex] = true;
            if(!isDependent && !Base->isDependentContext()){
                const ASTRecordLayout &baseLayout = Context->getASTRecordLayout(Base);
                CharUnits baseLeafSize = baseLayout.getSize();
                if(leafSize == baseLeafSize){
                    graph_phantoms[baseIndex][declIndex] = true;
                    graph_phantoms[declIndex][baseIndex] = true;
                }
            }
        }

        if(tempBaseVec.size() > 0){
            map<string, vector<string>>::iterator iteraRela = relations.find(declName);
            if(iteraRela == relations.end()){
                relations.insert(pair<string, vector<string>>(declName, tempBaseVec));
            }else{
                for(string tempStr : tempBaseVec){
                    if(find(iteraRela->second.begin(), iteraRela->second.end(), tempStr) == iteraRela->second.end()){
                        iteraRela->second.push_back(tempStr);
                    }
                }
            }
        }


    } // end if(hasVisitedField.find(declName) == hasVisitedField.end())

    return true;
  }

//   bool VisitClassTemplateSpecializationDecl(ClassTemplateSpecializationDecl *CD){
//       if(!CD || !CD->hasDefinition() || CD->isDependentContext())
//         {return true;
//         }
//       llvm::errs() << "template class " << CD->getDeclName() << "\n";
//       for(auto &Base : CD->bases()){
//           llvm::errs() << "aaaa\n";
//       }
//   }
  void addRelationShip(const BaseVec &bases, const CXXRecordDecl *current){

        vector<string> tempBaseVec;
        string ddname = current->getName().str();
        //map<string, int>::iterator declIter;
        int currentDeclIndex = 0;
        map<string, int>::iterator declIter = classCode.find(ddname);
        if(declIter != classCode.end()){
            currentDeclIndex = declIter->second;
        }else{
            currentDeclIndex = currentIndex;
            numberName.insert(pair<int, string>(currentIndex, ddname));
            classCode.insert(pair<string, int>(ddname, currentIndex));
            currentIndex++;
        }
       for(auto base : bases){
            int currentBaseIndex = 0;
            string baseName = base->getName().str();
            tempBaseVec.push_back(baseName);
            declIter = classCode.find(baseName);
            if(declIter != classCode.end()){
                currentBaseIndex = declIter->second;
            }else{
                currentBaseIndex = currentIndex;
                numberName.insert(pair<int, string>(currentIndex, baseName));
                classCode.insert(pair<string, int>(baseName, currentIndex));
                currentIndex++;
            }
            graph[currentDeclIndex][currentDeclIndex] = true;
            graph[currentBaseIndex][currentDeclIndex] = true;
            graph_family[currentBaseIndex][currentDeclIndex] = true;
            graph_family[currentDeclIndex][currentBaseIndex] = true;
            if(!current->isDependentContext() && !base->isDependentContext()){
                const ASTRecordLayout &l1 = Context->getASTRecordLayout(current);
                const ASTRecordLayout &l2 = Context->getASTRecordLayout(base);
                CharUnits l1Size = l1.getSize();
                CharUnits l2Size = l2.getSize();
                if(l1Size == l2Size){
                    graph_phantoms[currentDeclIndex][currentBaseIndex] = true;
                    graph_phantoms[currentBaseIndex][currentDeclIndex] = true;
                }
            }
        }

        if(tempBaseVec.size() > 0){
            map<string, vector<string>>::iterator iteraRela = relations.find(ddname);
            if(iteraRela == relations.end()){
                relations.insert(pair<string, vector<string>>(ddname, tempBaseVec));
            }else{
                for(string tempStr : tempBaseVec){
                    if(find(iteraRela->second.begin(), iteraRela->second.end(), tempStr) == iteraRela->second.end()){
                        iteraRela->second.push_back(tempStr);
                    }
                }
            }
        }


  }
  bool VisitCastExpr(CastExpr *CE){
	switch(CE->getCastKind()){

        case CK_BaseToDerived:
            {
                PresumedLoc PLoc = Context->getSourceManager().getPresumedLoc(CE->getLocStart());
                if(PLoc.isValid()){
                    //llvm::errs() << "DownCast occurs " << PLoc.getFilename() << " : " << PLoc.getLine() << "\n";
                    string tempLoc = PLoc.getFilename();
                    tempLoc += ":";
                    tempLoc += to_string(PLoc.getLine());
                    if(downCastLoc.find(tempLoc) == downCastLoc.end()){
                        downCastLoc.insert(tempLoc);
                    }
                }
                QualType derivedTy = CE->getType();
                const Expr* E = CE->getSubExpr();
                QualType basedTy = E->getType();
                const PointerType *derivedPty = dyn_cast<PointerType>(derivedTy.getTypePtr());
                const PointerType *basedPty = dyn_cast<PointerType>(basedTy.getTypePtr());
                
                const CXXRecordDecl *derivedCXX = derivedTy->getPointeeCXXRecordDecl();
                const CXXRecordDecl *basedCXX = basedTy->getPointeeCXXRecordDecl();
                //llvm::Type* tempType = convertType(basedTy);
                if(derivedCXX){
                            string ddname = derivedCXX->getName().str();
                            //llvm::errs() << "find the derived class " << ddname << "\n";
                            if(downCastVec.find(ddname) == downCastVec.end()){
                                //llvm::errs() << "inserted derived class " << ddname << "\n";
                                downCastVec.insert(ddname);
                            }
                            BaseVec baseVec;
                            CollectAllBases(derivedCXX, baseVec);
                            addRelationShip(baseVec, derivedCXX);
                            for(auto base : baseVec){
                                BaseVec baseVec2;
                                CollectAllBases(base, baseVec2);
                                addRelationShip(baseVec2, base);
                            }

                            if(castClass.find(ddname) == castClass.end()){
                                castClass.insert(ddname);
                            }
                }
                if(basedCXX){
                        string bbname = basedCXX->getName().str();
                        if(castClass.find(bbname) == castClass.end()){
                            castClass.insert(bbname);
                        }
                        if(basedCastClass.find(bbname) == basedCastClass.end()){
                            basedCastClass.insert(bbname);
                        }
                }
              
                break;
            }
        case CK_DerivedToBase:{
            const Expr* E = CE->getSubExpr();
            QualType derivedTy = E->getType();
            // const PointerType *basedPty = dyn_cast<PointerType>(basedTy.getTypePtr());
            const CXXRecordDecl *derivedCXX = derivedTy->getPointeeCXXRecordDecl();
            if(derivedCXX){
                string ddname = derivedCXX->getName().str();
                BaseVec baseVec;
                CollectAllBases(derivedCXX, baseVec);
                addRelationShip(baseVec, derivedCXX);
                for(auto base : baseVec){
                    BaseVec baseVec2;
                    CollectAllBases(base, baseVec2);
                    addRelationShip(baseVec2, base);
                }
            }
            break;
        }
        
        case CK_BitCast:{
            QualType DestTy = CE->getType();
            QualType SrcTy = CE->getSubExpr()->getType();
            auto DestPT = DestTy->getAs<PointerType>();
            if(DestPT){
                QualType DestPointee = DestPT->getPointeeType();
                auto *DestClassTy = DestPointee->getAs<RecordType>();
                if(DestClassTy){
                    const CXXRecordDecl *DestClassDecl = cast<CXXRecordDecl>(DestClassTy->getDecl());
                    if(DestClassDecl && DestClassDecl->isCompleteDefinition() && DestClassDecl->hasDefinition() && 
                    !DestClassDecl->isAnonymousStructOrUnion()){
                        BaseVec baseVec;
                        CollectAllBases(DestClassDecl, baseVec);
                        addRelationShip(baseVec, DestClassDecl);
                    }
                }
            }
            auto SrcPT = SrcTy->getAs<PointerType>();
            if(SrcPT){
                QualType SrcPointee = SrcPT->getPointeeType();
                auto *SrcClassTy = SrcPointee->getAs<RecordType>();
                if(SrcClassTy){
                    const CXXRecordDecl *SrcClassDecl = cast<CXXRecordDecl>(SrcClassTy->getDecl());
                    if(SrcClassDecl && SrcClassDecl->isCompleteDefinition() && SrcClassDecl->hasDefinition() &&
                    !SrcClassDecl->isAnonymousStructOrUnion()){
                        BaseVec baseVec;
                        CollectAllBases(SrcClassDecl, baseVec);
                        addRelationShip(baseVec, SrcClassDecl);
                    }
                }
            }
            break;
        }
		default:
		    return true;
        }
	return true;

  }

  static QualType getElemQualTypeOrNull(const QualType QT, bool &isCompoundElem) {
        const Type *Ty = QT.getTypePtrOrNull();

        if (!Ty)
            return NullQualType;

        if (Ty->isStructureOrClassType()) {
            isCompoundElem = true;
            return QT;
        } else if (const ArrayType *ATy = dyn_cast<ArrayType>(Ty)) {
            QualType ElemQT = ATy->getElementType();
            return getElemQualTypeOrNull(ElemQT, isCompoundElem);
        } else {
    // Do nothing for now.
        }
        return NullQualType;
        }

private:
  ASTContext *Context;
};

class FindNamedClassConsumer : public clang::SemaConsumer {
public:
  explicit FindNamedClassConsumer(ASTContext *Context)
    : Visitor(Context) {}

//   virtual void HandleTranslationUnit(clang::ASTContext &Context) {
//     Visitor.TraverseDecl(Context.getTranslationUnitDecl());
//   }
 virtual bool HandleTopLevelDecl(clang::DeclGroupRef DR) {
        for (DeclGroupRef::iterator b = DR.begin(), e = DR.end();
             b != e; ++b)
            // Traverse the declaration using our AST visitor.
            Visitor.TraverseDecl(*b);
        return true;
    }

private:
  FindNamedClassVisitor Visitor;
};

class FindNamedClassAction : public clang::ASTFrontendAction {
public:
  virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
    clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
    return std::unique_ptr<clang::ASTConsumer>(
        new FindNamedClassConsumer(&Compiler.getASTContext()));
  }
};


//DFS to find the relationship graph
void DFS(vector<vector<int>> &result, bool** graphtemp){
        
        
        bool visited_bitmap[MAX_NUM_CLASS] = {false};

        int startNode = 0;

        while(true){

            stack<int> s;
            //find the node that has not been visited
            while(visited_bitmap[startNode] && startNode < currentIndex){
                startNode++;
            }

            //all node has been visited, return the function
            if(startNode == currentIndex){
                return;
            }

            vector<int> currentSet;
            //int currentNode = startNode;
            s.push(startNode);
            visited_bitmap[startNode] = 1;
            while(!s.empty()){
                int v = s.top();
                s.pop();
                //visite the node
                currentSet.push_back(v);

                for(int i = 0; i < currentIndex; i++){
                    if(graphtemp[v][i] && !visited_bitmap[i]){
                        s.push(i);
                        visited_bitmap[i] = true;
                    }// end if
                }//end for

            }// end while(!s.empty())

            if(currentSet.size() >= 1)
                result.push_back(currentSet);
        }
        //Find the node that hasn't been visited
    
        }


void filterDownCast(const vector<vector<int>> result){
    int familyNum = 1;
    for(vector<int> tempVector : result){
        int tempSize = tempVector.size();
        int count = 0;
        for(int i = 0; i < tempSize; i++){
            map<int, string>::iterator it = numberName.find(tempVector[i]);
            
            if(it == numberName.end()){
                llvm::errs() << "Cann't find the number\n";
                return;
            }
            string name = it->second;
            set<string>::iterator downIt = downCastVec.find(name);
            if(downIt == downCastVec.end()){
                continue;
            }
            count++;
        }
        llvm::errs() << "family " << familyNum++ << "counts " << count << "\n";
    }
}
//print the relation set to a file
int output(const vector<vector<int>> result, string fileName){
    ofstream outfile;
    outfile.open(fileName, ios::out | ios::trunc);
    if(!outfile){
        llvm::errs() << "Cann't open the file " << fileName << "\n";
        return -1;
    }
    int familyNum = 1;
    for(vector<int> tempVector : result){
        int tempSize = tempVector.size();
        llvm::errs() << "family " << familyNum++ << "counts " << tempSize << "\n";
        for(int i = 0; i < tempSize; i++){
            map<int, string>::iterator it = numberName.find(tempVector[i]);
            if(it == numberName.end()){
                llvm::errs() << "Cann't find the number\n";
                return -1;
            }
            string name = (it->second);
            outfile << name;
            if(i == tempSize - 1){
                outfile << "\n";
            }else{
                outfile << ",";
            }
        }//end for(int = 0; i < tempSize; i++)
    }//end for(vector<int> tempVector : result)
    outfile.close();
    return 1;
}

int outputCodeResult(const map<int, int> result, string fileName){
    ofstream outfile;
    outfile.open(fileName, ios::out | ios::trunc);
    if(!outfile){
        llvm::errs() << "Cann't open the file " << fileName << "\n";
        return -1;
    }
    for(pair<int, int> rowResult : result){
        int currentClass = rowResult.first;
        map<int, string>::iterator nameIter = numberName.find(currentClass);
        if(nameIter == numberName.end()){
            llvm::errs() << "Error, the class index does not included by numberName\n";
            exit(-1); 
        }
        string className = nameIter->second;
        outfile << className << ",";
        outfile << rowResult.second<< "\n";
        //outfile << rowResult.second.sum << "\n";
    }
    outfile.close();
}

int outputCastedRelatedSet(const map<int, int> result, string fileName, string fileName2){
    //int minSize = 0xfffffff;
    ofstream outfile;
    outfile.open(fileName, ios::out | ios::trunc);
    if(!outfile){
        llvm::errs() << "Cann't open the file " << fileName << "\n";
        return -1;
    }


    ofstream outfile2;
    outfile2.open(fileName2, ios::out | ios::trunc);
    if(!outfile2){
        llvm::errs() << "Cann't open the file " << fileName2 << "\n";
        return -1;
    }

    for(pair<int, int> rowResult : result){
        int currentClass = rowResult.first;
        map<int, string>::iterator nameIter = numberName.find(currentClass);
        if(nameIter == numberName.end()){
            llvm::errs() << "Error, the class index does not included by numberName\n";
            exit(-1);
        }
        //llvm::errs() << "Struct Alignment :\n";
        string className = nameIter->second;
        if(castrelatedClass.find(className) == castrelatedClass.end())
            continue;

        if(rowResult.second > 0){
            //if(castClass.find(className) != castClass.end()){
                outfile << className << ",";
                outfile << rowResult.second << "\n";
                
            //}

            map<string, int>::iterator classIter = classSize.find(className);
            if(classIter != classSize.end()){
                //outfile2 << classIter->second << "\n";
                outfile2 << className << " : " << classIter->second << "\n";
            }
        
            }
        
        
    }
    outfile.close();
    return 1;
}

int outputBases(string fileName){
    ofstream outfile;
    outfile.open(fileName, ios::out | ios::trunc);
    if(!outfile){
        llvm::errs() << "Cann't open the file " << fileName << "\n";
        return -1;
    }
    for(pair<string, vector<string>> tempRelation : relations){
        outfile << tempRelation.first;
        for(string base : tempRelation.second){
            outfile << "," << base;
        }
        outfile << "\n";
    }
    outfile.close();
    return 1;
}

// void getTheMinSize(){

// }
// print the safe cast set

int outputDownCastLoc(string fileName){
    ofstream outfile;
    outfile.open(fileName, ios::out | ios::trunc);
    if(!outfile){
        llvm::errs() << "Cann't open the file" << fileName << "\n";
        return -1;
    }
    int tempCurrentIndex = 0;
    for(string tempLoc : downCastLoc){
        outfile << tempLoc << "," << tempCurrentIndex << endl;
        tempCurrentIndex++;
    }
    outfile.close();
    return 1;
}

int outputSafeCast(string fileName){
    ofstream outfile;
    outfile.open(fileName, ios::out | ios:: trunc);
    if(!outfile){
        llvm::errs() << "Cann't open the file " << fileName << "\n";
        return -1;
    }

    for(pair<string, vector<string>> tempSet : relations){
        string currentClass = tempSet.first;
        vector<string> castedSet = tempSet.second;

        if(castrelatedClass.find(currentClass) != castrelatedClass.end()){
            outfile << currentClass;
            for(string tempclass : castedSet){
                if(downCastVec.find(tempclass) != downCastVec.end()){
                    outfile << "," << tempclass;
                }
            }
            outfile << "\n";
        }
    }

    outfile.close();
    return 1;
}

//get the file in the directory
int getFiles(string directory, vector<string>& files){
    set<string> blackList;
    blackList.insert(string("test"));
    blackList.insert(string("unittests"));

    DIR* dir = opendir(directory.c_str());
    if(dir == NULL){
        files.push_back(directory);
        llvm::errs() << directory << " is not a directory or not exits!\n";
        return -1;
    }

    struct dirent* d_ent = NULL;
    //char fullPath[128] = {0};
    char dot[3] = ".";
    char dotdot[6] = "..";
    while((d_ent = readdir(dir)) != NULL){
        if((strcmp(d_ent->d_name, dot) != 0) && (strcmp(d_ent->d_name, dotdot) != 0)){
            if(d_ent->d_type == DT_DIR){
                string dirName = string(d_ent->d_name);
                
            if(dirName.find("test") == std::string::npos){
                string newDirectory = directory + string("/") + string(d_ent->d_name);
                if(directory[directory.length()-1] == '/'){
                    newDirectory = directory + string(d_ent->d_name);
                }

                if(-1 == getFiles(newDirectory, files)){
                    return -1;
                }
            }
          
        } //end if(d_ent->d_type== DT_DIR)
        else{
            string filename = string(d_ent->d_name);
            string suffixStr = filename.substr(filename.find_last_of('.') + 1); // get the file suffix
            if(suffixStr == string("cpp") || suffixStr == string("cc") ||
             suffixStr == string("cxx") || suffixStr == string("c++") || suffixStr == string("C")
            ){
                string absolutePath = directory + string("/") + string(d_ent->d_name);
                if(directory[directory.length()-1] == '/'){
                    absolutePath = directory + string(d_ent->d_name);
                }
                files.push_back(absolutePath);
            }
        } // end else
            
        }
    }
    closedir(dir);
    return 0;
}

void filter(vector<vector<int>> result, vector<vector<int>>& filtedResult){
    for(vector<vector<int>>::iterator resultItera = result.begin(); resultItera != result.end(); resultItera++){
        vector<int> temp = *resultItera;
        vector<int> tempFilted;
        for(vector<int>::iterator tempItera = temp.begin(); tempItera != temp.end(); tempItera++){
            string className;
            map<int, string>::iterator numItera = numberName.find(*tempItera);
            if(numItera != numberName.end()){
                className = numItera->second;
            }else{
                continue;
            }
            if(downCastVec.find(className) != downCastVec.end()){
                tempFilted.push_back(*tempItera);
            }
        }// end for(vector<int>::iterator tempItera = temp.begin();)
        
        if(tempFilted.size() > 0){
            filtedResult.push_back(tempFilted);
        }
    }
}

void codingFunc(vector<vector<int>> filteredFamily, vector<vector<int>> phantomSet, map<int, int> &result, map<int, int> &relatedResult){
    //map<int, codeStructS> havingCode;
    for(vector<int> tempVec : filteredFamily){
        int size = tempVec.size();
        int familySize = 0;
        int currentIndex = 1;
        for(int i = 0; i < size; i++){
            int currentClass = tempVec[i];
            map<int, string>::iterator numItera = numberName.find(currentClass);
            string className;
            if(numItera != numberName.end()){
                className = numItera->second;
            }

            if(downCastVec.find(className) == downCastVec.end()){
                continue;
            }

            if(result.find(currentClass) != result.end()){
                continue;
            }
            codeStruct tempCode;
            tempCode.index = currentIndex;
            // the phantom classes share the same serial number
            for(vector<int> tempPhantom : phantomSet){
                vector<int>::iterator finded = std::find(tempPhantom.begin(), tempPhantom.end(), currentClass);
                if(finded != tempPhantom.end()){
                    for(int tempClass : tempPhantom){
                        result.insert(pair<int, int>(tempClass, currentIndex));
                    }
                    break;
                }
            }
            result.insert(pair<int, int>(currentClass, currentIndex));
            familySize = currentIndex;
            currentIndex++;
        }

        for(int i = 0; i < size; i++){
            map<int, codeStruct>::iterator resultIter;
            int currentClass = tempVec[i];
            relatedResult.insert(pair<int, int>(currentClass, familySize));
        }

    }
}


void getAllBases(){
    for(auto tempBase : basesVec){
        string currentName = tempBase.first;
        llvm::errs() << "Get the base of " << currentName << "\n";
        if(relations.find(currentName) == relations.end()){
            //vector<string>* tempVecPtr = &(tempBase.second);
            vector<string> tempVec;
            SmallVector<string, 8> Queue;
            string tempClassName = currentName;
            while(true){
                map<string, vector<string>>::iterator tempIter = basesVec.find(tempClassName);
                if(tempIter != basesVec.end()){
                     vector<string> tempBases = tempIter->second;
                     for(auto baseName : tempBases){
                        if(find(tempVec.begin(), tempVec.end(), baseName) == tempVec.end()){
                            Queue.push_back(baseName);
                            tempVec.push_back(baseName);}
                            else{
                            llvm::errs() << "The class " << baseName << " has been contained in the base set.\n";
                        }
                    }
                }
               
                if(Queue.empty())
                    break;
                tempClassName = Queue.pop_back_val();
            }
            
            if(tempVec.size() > 0){
                relations.insert(pair<string, vector<string>>(currentName, tempVec));
            }
        }
        
    }
}

void calculateRelatedClass(){
    for(string baseClass : basedCastClass){
        if(castrelatedClass.find(baseClass) == castrelatedClass.end())
            castrelatedClass.insert(baseClass);
        
        for(pair<string, vector<string>> tempRelation : relations){
            string tempClass = tempRelation.first;
            if(castrelatedClass.find(tempClass) != castrelatedClass.end())
                continue;

            if(find(tempRelation.second.begin(), tempRelation.second.end(), baseClass) != tempRelation.second.end()){
                castrelatedClass.insert(tempClass);
            }
        }
    }
    for(string downClass : downCastVec){
        if(castrelatedClass.find(downClass) == castrelatedClass.end()){
            llvm::errs() << "don't contain class " << downClass <<"\n";
            castrelatedClass.insert(downClass); 
        }
    }
}


void filterCast(set<int>& castedresult, vector<vector<int>> castVec){
    for(string downcastStr : downCastVec){
        map<string, int>::iterator classCodeIter = classCode.find(downcastStr);
        if(classCodeIter != classCode.end()){
            int currentClass = classCodeIter->second;
            if(castedresult.find(currentClass) == castedresult.end()){
                for(vector<int> tempVec : castVec){
                    bool doesContain = false;
                    for(int tempCode : tempVec){
                        if(tempCode == currentClass){
                            doesContain = true;
                            break;
                        }
                    }
                    if(doesContain){
                        for(int tempCode : tempVec){
                            castedresult.insert(tempCode);
                        }
                    }
                }
            }
        }
    }
}

void initgraph(){
    graph = new bool*[MAX_NUM_CLASS];
    graph_phantoms = new bool*[MAX_NUM_CLASS];
    graph_family = new bool*[MAX_NUM_CLASS];
    for(int i = 0; i < MAX_NUM_CLASS; i++){
        graph[i] = new bool[MAX_NUM_CLASS];
        graph_family[i] = new bool[MAX_NUM_CLASS];
        graph_phantoms[i] = new bool[MAX_NUM_CLASS];
        memset(graph[i], 0, sizeof(bool) * MAX_NUM_CLASS);
        memset(graph_family[i], 0, sizeof(bool) * MAX_NUM_CLASS);
        memset(graph_phantoms[i], 0, sizeof(bool) * MAX_NUM_CLASS);

    }
}
static llvm::cl::OptionCategory MyToolCategory("-p: to specify the complication database\n -f: to specify the parse file or directory");


int main(int argc,char **argv) {
  if (argc > 1) {
    CommonOptionsParser op(argc, const_cast<const char **>(argv), MyToolCategory);        
    // create a new Clang Tool instance (a LibTooling environment)

    // std::vector<std::string> filelist;
    // filelist.push_back(std::string(argv[1]));


	vector<string> fileLists;

    char ch;
    char* directory = NULL;
    char* outFile = NULL;
    char* safeCastFile = NULL;
    // while((ch == getopt(argc, argv, "o:p:f:h")) != -1){
    //     switch(ch){
    //         case 'p':
    //             break;
    //         case 'f':
    //             directory = optarg;
    //             break;
    //         case 'o':
    //             outFile = optarg;
    //             break;
    //         case 'h':
    //             llvm::errs() << "-p: to specify the complication database\n -f to spacify file or directory to parse\n -o to specify the output file name";
    //             return -1;
    //             break;
            
    //     }
    // }

    directory = argv[3];
    if(directory == NULL){
        llvm::errs() << "Please set the options -f to identify which file or directory to parse\n";
        return -1;
    }
    string outFileStr;
    if(!outFile){
        DIR* dir = opendir(directory);
        if(dir == NULL){
            llvm::errs() << "Please give a directory\n";
        }else{
            string tempDir = string(directory);
            if(tempDir[tempDir.length()-1] == '/'){
                outFileStr = tempDir + string("coding-num.txt");
            }else{
                outFileStr = tempDir + string("/coding-num.txt");
            }
        }
        closedir(dir);

    }else{
        outFileStr = string(outFile);
    }

    string outFileSafeSet;
    if(!safeCastFile){
        DIR* dir = opendir(directory);
        if(dir == NULL){
            llvm::errs() << "Please give a directory\n";
        }else{
            string tempDir = string(directory);
            if(tempDir[tempDir.length()-1] == '/'){
                outFileSafeSet = tempDir + string("safecast.txt");
            }else{
                outFileSafeSet = tempDir + string("/safecast.txt");
            }
        }
        closedir(dir);
    }else{
        outFileSafeSet = string(safeCastFile);
    }

    // if(argc < 4){
    //     errs() << "Please configure the build directory\n";
    //     return -1;
    // }
    getFiles(string(directory), fileLists);
    llvm::errs() << "File List contents:\n";
    int count = fileLists.size();
    for(int i = 0; i < count; i++){
         llvm::errs() << fileLists[i];
         llvm::errs() << ", ";
    }
    llvm::errs() << "\nEnd File list contents\n";
    initgraph();
    ClangTool Tool(op.getCompilations(), fileLists);

    // run the Clang Tool, creating a new FrontendAction (explained below)
    Tool.run(newFrontendActionFactory<FindNamedClassAction>().get());

    //getAllBases();
    //initializeCastClass();
    calculateRelatedClass();
    vector<vector<int>> result;

    

    DFS(result, graph);

    vector<vector<int>> phantomSets;

    DFS(phantomSets, graph_phantoms);

    vector<vector<int>> castedVec;
    DFS(castedVec, graph_family);

    //set<int> castedResult;
    //filterCast(castedResult, castedVec);

    vector<vector<int>> filtedResult;
    llvm::errs() << "Output file name " << outFileStr << "\n";
    //filter(result, filtedResult);

    //Debug
    int familyIndex = 1;
    for(vector<int> tempresult : filtedResult){
        llvm::errs() << "Family " << familyIndex << " : " << tempresult.size() << "\n"; 
    }
    //Debug

    //vector<vector<int>> filtedAAAResult;
    //filter(phantomSets, filtedAAAResult);
    //output(filtedResult, outFileStr);

    map<int, int> codeResult;

    map<int, int> relatedResult;
    codingFunc(result, phantomSets, codeResult, relatedResult);

    outputCodeResult(codeResult, outFileStr);

    string tempDirA = string(directory);

    outputCastedRelatedSet(relatedResult, tempDirA+"/castrelated-set.txt", tempDirA + "/class_alignment.txt");

    outputSafeCast(outFileSafeSet);

    
    outputBases(tempDirA + "/bases-set.txt");

    outputDownCastLoc(tempDirA + "/downcastLoc.txt");

    llvm::errs() << "All class number is " << currentIndex << "\n";

    llvm::errs() << "Cast related class number is " << castrelatedClass.size() << "\n";
    // for(string castStr : castrelatedClass){
    //     llvm::errs() << "related class : " << castStr << "\n";
    // }
    return 0;
		
  }
}