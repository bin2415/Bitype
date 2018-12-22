//===-- ClangdFuzzer.cpp - Fuzz clangd ------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief This file implements a function that runs clangd on a single input.
/// This function is then linked into the Fuzzer library.
///
//===----------------------------------------------------------------------===//

#include "ClangdLSPServer.h"
#include "ClangdServer.h"
#include "CodeComplete.h"
#include <sstream>

extern "C" int LLVMFuzzerTestOneInput(uint8_t *data, size_t size) {
  clang::clangd::JSONOutput Out(llvm::nulls(), llvm::nulls(), nullptr);
  clang::clangd::CodeCompleteOptions CCOpts;
  CCOpts.EnableSnippets = false;
  clang::clangd::ClangdServer::Options Opts;

  // Initialize and run ClangdLSPServer.
  clang::clangd::ClangdLSPServer LSPServer(Out, CCOpts, llvm::None, Opts);

  std::istringstream In(std::string(reinterpret_cast<char *>(data), size));
  LSPServer.run(In);
  return 0;
}
