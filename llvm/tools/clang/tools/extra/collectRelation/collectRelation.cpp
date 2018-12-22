#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/Support/raw_ostream.h"
#include "clang/AST/ASTContext.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/CodeGen/CodeGenAction.h"

#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <iostream>
#include <set>
#include <stack>
#include <unistd.h>
#include <fstream>

using namespace clang::tooling;
using namespace clang::ast_matchers;
using namespace clang;
using namespace llvm;
using namespace std;


namespace{


 namespace {
 AST_MATCHER(CXXRecordDecl, hasbases) {
   if (Node.hasDefinition())
     return Node.getNumBases() > 0;
   return false;
 }
 }

#define MAX_NUM_CLASS 20000

static QualType NullQualType;

set<string> castObj; //store the cast-related object according to the static_cast and dynamic_cast operator
map<string, set<string>> inheritation; //inheritation
set<string> hasVisitedCom; //combination visited
set<string> hasVisitedInh; //inherit visited
map<string, int> classCode;
bool graph[MAX_NUM_CLASS][MAX_NUM_CLASS];
static int currentIndex = 0;

class CxxRecordRelation : public MatchFinder::MatchCallback{
    public:
        virtual void run(const MatchFinder::MatchResult &Result){
             if(const CXXRecordDecl *D = Result.Nodes.getNodeAs<CXXRecordDecl>("cxxRecord")){
                 string recordName = D->getName().str();
                 set<string>::iterator visitedIter;
                 visitedIter = hasVisitedCom.find(recordName)
                 bool firstRecord = true;
                 int recordIndex = 0;
                 if(!D->isPolymorphic() && visitedIter == hasVisitedCom.end()){
                    for(const auto *FD: D->fields()){
                     //reference https://github.com/sslab-gatech/caver

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

                     map<string, int>::iterator codeIter;
                     codeIter = classCode.find(recordName);
                     if(firstRecord){
                         firstRecord = false;
                         if(codeIter = classCode.end()){
                             recordIndex = currentIndex;
                             classCode.insert(pair<string, int>(recordName, currentIndex++));
                         }else{
                             recordIndex = codeIter->second;
                         }
                     }

                     string fieldName = FD->getName().str();
                     int fieldIndex = 0;
                     codeIter = classCode.find(fieldName);
                     if(codeIter == classCode.end()){
                         fieldIndex = currentIndex;
                         classCode.insert(pair<string, int>(fieldName, currentIndex));
                     }else{
                         fieldIndex = codeIter->second;
                     }
                     graph[recordIndex][fieldIndex] = true;
                     graph[fieldIndex][recordIndex] = true;
                     
                 }// end for(const auto)
             }
             
             }

        }

    // static QualType getElemQualTypeOrNull(const QualType QT, bool &isCompoundElem) {
    //     const Type *Ty = QT.getTypePtrOrNull();

    //     if (!Ty)
    //         return NullQualType;

    //     if (Ty->isStructureOrClassType()) {
    //         isCompoundElem = true;
    //         return QT;
    //     } else if (const ArrayType *ATy = dyn_cast<ArrayType>(Ty)) {
    //         QualType ElemQT = ATy->getElementType();
    //         return getElemQualTypeOrNull(ElemQT, isCompoundElem);
    //     } else {
    // // Do nothing for now.
    //     }
    //     return NullQualType;
    //     }
      
};

// class CxxStaticCast : public MatchFinder::MatchCallback{
//     public:
//     virtual void run(const MatcherFinder::MatchResult &result){

//     }
// };

// class CxxDynamicCast : public MatchFinder::MatchCallback{
//     public:
//     virtual void run(const MatcherFinder::MatchResult &result){

//     }
// };

}// end namespace

//print the relation set to a file
// int output(const vector<vector<int>> result, string fileName){
//     ofstream outfile;
//     outfile.open(fileName, ios::out | ios:: trunc);
//     if(!outfile){
//         errs() << "Cann't open the file " << fileName << "\n";
//         return -1;
//     }
//     for(vector<int> tempVector : result){
//         int tempSize = tempVector.size();
//         for(int i = 0; i < tempSize; i++){
//             map<int, string>::iterator it = number_to_name.find(tempVector[i]);
//             if(it == number_to_name.end()){
//                 errs() << "Cann't find the number\n";
//                 return -1;
//             }
//             string name = (it->second);
//             outfile << name;
//             if(i == tempSize - 1){
//                 outfile << "\n";
//             }else{
//                 outfile << ",";
//             }
//         }//end for(int = 0; i < tempSize; i++)
//     }//end for(vector<int> tempVector : result)
//     return 1;
// }

//get the file in the directory
int getFiles(string directory, vector<string>& files){
    set<string> blackList;
    blackList.insert(string("test"));
    blackList.insert(string("unittests"));

    DIR* dir = opendir(directory.c_str());
    if(dir == NULL){
        files.push_back(directory);
        cout << directory << " is not a directory or not exits!" << endl;
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
                
            if(blackList.find(dirName) == blackList.end()){
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
            if(suffixStr == string("cpp") || suffixStr == string("h") || suffixStr == string("cc")
            || suffixStr == string("hpp") || suffixStr == string("hxx") || suffixStr == string("cxx") ||
            suffixStr == string("c++")
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


static llvm::cl::OptionCategory MyToolCategory("-p: to specify the complication database\n -f: to specify the parse file or directory");


int main(int argc, char *argv[]){


    CommonOptionsParser OptionsParser(argc, const_cast<const char **>(argv), MyToolCategory);
    vector<string> fileLists;

    char ch;
    char* directory = NULL;
    char* outFile = NULL;
    while((ch == getopt(argc, argv, "o:p:f:h")) != -1){
        switch(ch){
            case 'p':
                break;
            case 'f':
                directory = optarg;
                break;
            case 'o':
                outFile = optarg;
                break;
            case 'h':
                errs() << "-p: to specify the complication database\n -f to spacify file or directory to parse\n -o to specify the output file name";
                return -1;
                break;
        }
    }

    directory = argv[1];
    if(directory == NULL){
        errs() << "Please set the options -f to identify which file or directory to parse\n";
        return -1;
    }
    string outFileStr;
    if(outFile){
        DIR* dir = opendir(directory);
        if(dir == NULL){
            errs() << "Please give a directory\n";
        }else{
            string tempDir = string(directory);
            if(tempDir[tempDir.length()-1] == '/'){
                outFileStr = tempDir + string("relationship-set.txt");
            }else{
                outFileStr = tempDir + string("/relationship-set.txt");
            }
        }
        closedir(dir);

    }
    // if(argc < 4){
    //     errs() << "Please configure the build directory\n";
    //     return -1;
    // }
    getFiles(string(directory), fileLists);
    errs() << "File List contents:\n";
    int count = fileLists.size();
    for(int i = 0; i < count; i++){
         errs() << fileLists[i];
         errs() << ", ";
    }
    errs() << "\nEnd File list contents\n";

    ClangTool Tool(OptionsParser.getCompilations(), fileLists);
    //errs() << "Crash on the ClangTool construction\n";
                    
    CxxRecordRelation recordAction;
    MatchFinder Finder;
    Finder.addMatcher(cxxRecordDecl().bind("cxxRecord"), &recordAction);
    
    //errs() << "The run is crash ?!\n";
    Tool.run(newFrontendActionFactory<CodeGenAction>().get());

    //vector<vector<int>> result;

    //search the relationship graph, find the tarjan
    //DFS(result);

    
    //variable result stores the relationship set
    //output(result, outFileStr);
    //cout << "The total class number is " << getSize();
    return 1;
}


