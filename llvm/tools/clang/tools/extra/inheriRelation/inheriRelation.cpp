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

#define MAX_NUM_CLASS 10000

map<string, int> visitedMap; //string to number
map<int, string> number_to_name; //number to string
set<string> hasVisited;
//vector<set<string>> relation_set;

bool allGraph[MAX_NUM_CLASS][MAX_NUM_CLASS] = {false};
int maxIndex = 0;

int getSize(){
    return visitedMap.size();
}
//Matcher
class CxxRecordRelation : public MatchFinder::MatchCallback{
    public:
        virtual void run(const MatchFinder::MatchResult &Result){
            //llvm::errs() << "I am running\n";
            if(const CXXRecordDecl *D = Result.Nodes.getNodeAs<CXXRecordDecl>("cxxRecord")){
                //llvm::errs() << "Class : " << D->getName() << "'s base is :"; 
                //string dName1 = QualType::getAsString(D->getASTContext().getTypeDeclType(const_cast<CXXRecordDecl*>(D)).split());
                if((!D) || !D->isCompleteDefinition() || !D->hasDefinition() || D->isAnonymousStructOrUnion()){
                    return;
                }
                string dName = D->getName().str();
                //determine if D has visited
                if(hasVisited.find(dName) != hasVisited.end()){
                    //llvm::errs() << "Find the class already visited\n";
                    return;
                }

                bool isFirst = true;
                int currentIndex = 0;
                for(const auto &I : D->bases()){
                    int baseIndex = 0;
                    const RecordType *Ty = I.getType()->getAs<RecordType>();
                    if(!Ty){
                        continue;
                    }

                    CXXRecordDecl *Base = cast_or_null<CXXRecordDecl>(Ty->getDecl()->getDefinition());
                    if(!Base || (Base->isDependentContext() && !Base->isCurrentInstantiation(D))|| 
                    !Base->isCompleteDefinition() || !Base->hasDefinition() || Base->isAnonymousStructOrUnion()){
                        continue;
                    }

                    string baseName = Base->getName().str();

                    // if the class's name if equal base name. e.g. template class
                    if(dName == baseName){
                        continue;
                    }
                    //Add to the visitedMap
                    if(isFirst){
                        hasVisited.insert(dName);
                        isFirst = false;
                        map<string, int>::iterator it = visitedMap.find(dName);
                        llvm::errs() << "current class is: " << dName << "\n";
                        if(it == visitedMap.end()){
                            currentIndex = maxIndex;
                            visitedMap.insert(pair<string, int>(dName, maxIndex));
                            number_to_name.insert(pair<int, string>(maxIndex++, dName));
                        }else{
                            currentIndex = it->second;
                        }
                    }
                    //string baseName1 =  QualType::getAsString(Base->getASTContext().getTypeDeclType(const_cast<CXXRecordDecl*>(Base)).split());
                    
                    map<string, int>::iterator cursor = visitedMap.find(baseName);
                    if(cursor == visitedMap.end()){
                        baseIndex = maxIndex;
                        visitedMap.insert(pair<string, int>(baseName, maxIndex));
                        number_to_name.insert(pair<int, string>(maxIndex++, baseName));
                    }else{
                        baseIndex = cursor->second;
                    }

                    allGraph[currentIndex][baseIndex] = true;
                    allGraph[baseIndex][currentIndex] = true;

                    //llvm::errs() << Base->getName() << ",";
                }
                //llvm::errs() << "\n";
            }
        }


      
};

//DFS to find the relationship graph
void DFS(vector<vector<int>> &result){
        
        
        bool visited_bitmap[MAX_NUM_CLASS] = {false};

        int startNode = 0;

        while(true){

            stack<int> s;
            //find the node that has not been visited
            while(visited_bitmap[startNode] && startNode < maxIndex){
                startNode++;
            }

            //all node has been visited, return the function
            if(startNode == maxIndex){
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

                for(int i = 0; i < maxIndex; i++){
                    if(allGraph[v][i] && !visited_bitmap[i]){
                        s.push(i);
                        visited_bitmap[i] = true;
                    }// end if
                }//end for

            }// end while(!s.empty())

            result.push_back(currentSet);
        }
        //Find the node that hasn't been visited
    
        }
}// end namespace

//print the relation set to a file
int output(const vector<vector<int>> result, string fileName){
    ofstream outfile;
    outfile.open(fileName, ios::out | ios:: trunc);
    if(!outfile){
        errs() << "Cann't open the file " << fileName << "\n";
        return -1;
    }
    for(vector<int> tempVector : result){
        int tempSize = tempVector.size();
        for(int i = 0; i < tempSize; i++){
            map<int, string>::iterator it = number_to_name.find(tempVector[i]);
            if(it == number_to_name.end()){
                errs() << "Cann't find the number\n";
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
    Finder.addMatcher(cxxRecordDecl(hasbases()).bind("cxxRecord"), &recordAction);
    
    //errs() << "The run is crash ?!\n";
    Tool.run(newFrontendActionFactory(&Finder).get());

    vector<vector<int>> result;

    //search the relationship graph, find the tarjan
    DFS(result);

    
    //variable result stores the relationship set
    output(result, outFileStr);
    cout << "The total class number is " << getSize();
    return 1;
}


