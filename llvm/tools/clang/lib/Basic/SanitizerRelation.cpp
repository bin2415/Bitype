#include "clang/Basic/SanitizerRelation.h"
#include "llvm/Support/MemoryBuffer.h"
#include <vector>
#include <fstream>

using namespace clang;




std::unique_ptr<std::map<std::string, unsigned>> SanitizerRelation::createCodeMap(std::string relationPath){

    std::unique_ptr<std::map<std::string, unsigned>> result(new std::map<std::string, unsigned>());
    std::ifstream ifile;
    ifile.open(relationPath, std::ios::in);

    std::string tempS;
    while(std::getline(ifile, tempS)){
        StringRef I(tempS);
        // Ignore empty lines
        if(I.empty()){
            continue;
        }
        std::vector<std::string> tempVec;

        std::pair<StringRef, StringRef> SplitLine = I.split(',');
        std::pair<StringRef, StringRef> codeNum = SplitLine.second.split(',');
        std::string className = SplitLine.first.str();
        unsigned codeNum1 = (unsigned)std::stoi(codeNum.first.str());
        result->insert(std::pair<std::string, unsigned>(className, codeNum1));
    }
    
    return result;
}

std::unique_ptr<std::map<std::string, unsigned>> SanitizerRelation::getClassIndex(std::string relationPath){

    std::unique_ptr<std::map<std::string, unsigned>> result(new std::map<std::string, unsigned>());

    unsigned currentIndex = 0;
    std::ifstream ifile;
    ifile.open(relationPath, std::ios::in);
    std::string tempS;
    while(std::getline(ifile, tempS)){
        // Ignore empty lines
        StringRef I(tempS);
        if(I.empty()){
            continue;
        }
        std::vector<std::string> tempVec;

        std::pair<StringRef, StringRef> SplitLine = I.split(',');
        std::string className = SplitLine.first.str();
        //unsigned codeNum1 = (unsigned)std::stoi(codeNum.second.str());
        result->insert(std::pair<std::string, unsigned>(className, currentIndex++));
    }
    
    return result;
}

std::unique_ptr<std::map<std::string, unsigned>> SanitizerRelation::getDebugInfo(std::string relationPath){
    std::unique_ptr<std::map<std::string, unsigned>> result(new std::map<std::string, unsigned>());
    std::ifstream ifile;
    ifile.open(relationPath, std::ios::in);
    std::string tempS;
    while(std::getline(ifile, tempS)){
        StringRef I(tempS);
        if(I.empty())
            continue;
        
        std::vector<std::string> tempVec;
        std::pair<StringRef, StringRef> SplitLine = I.split(',');
        std::string className = SplitLine.first.str();
        std::string refNum = SplitLine.second.str();

        unsigned codeNum1 = (unsigned)std::stoi(refNum);
        result->insert(std::pair<std::string, unsigned>(className, codeNum1));
        //llvm::errs() << "get the loc" << className << " : " << codeNum1 << "\n";
    }
    return result;
    
}


