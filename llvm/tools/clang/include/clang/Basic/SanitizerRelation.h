// This file is distributed under the University of Illinois Open Source
// License. See LINCESE.TXT for details.
//
// === ------------------------------------------------------------===//
// 
//  Read the class inheritance relationship from the class
//

#ifndef LLVM_CLASS_BASIC_SANITIZERRELATION_H
#define LLVM_CLASS_BASIC_SANITIZERRELATION_H

#include "clang/Basic/LLVM.h"
#include "clang/Basic/Sanitizers.h"
#include <memory>
#include <set>
#include <map>

namespace clang{

// struct codeInfo{
//     int index;  // which bit is one
//     int sumNum; // the family's class number
// };

class SanitizerRelation{
    //const std::string relationPath;
    public:
        
        //SanitizerRelation(const std::string path);

        //std::unique_ptr<std::set<std::string>> createNameSet();

        std::unique_ptr<std::map<std::string, unsigned>> createCodeMap(std::string);
        std::unique_ptr<std::map<std::string, unsigned>> getClassIndex(std::string);
        std::unique_ptr<std::map<std::string, unsigned>> getDebugInfo(std::string);
};

}

#endif