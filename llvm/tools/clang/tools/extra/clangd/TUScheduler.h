//===--- TUScheduler.h -------------------------------------------*-C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANGD_TUSCHEDULER_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANGD_TUSCHEDULER_H

#include "ClangdUnit.h"
#include "Function.h"
#include "Threading.h"
#include "llvm/ADT/StringMap.h"

namespace clang {
namespace clangd {

/// Returns a number of a default async threads to use for TUScheduler.
/// Returned value is always >= 1 (i.e. will not cause requests to be processed
/// synchronously).
unsigned getDefaultAsyncThreadsCount();

struct InputsAndAST {
  const ParseInputs &Inputs;
  ParsedAST &AST;
};

struct InputsAndPreamble {
  llvm::StringRef Contents;
  const tooling::CompileCommand &Command;
  const PreambleData *Preamble;
};

/// Determines whether diagnostics should be generated for a file snapshot.
enum class WantDiagnostics {
  Yes,  /// Diagnostics must be generated for this snapshot.
  No,   /// Diagnostics must not be generated for this snapshot.
  Auto, /// Diagnostics must be generated for this snapshot or a subsequent one,
        /// within a bounded amount of time.
};

/// Handles running tasks for ClangdServer and managing the resources (e.g.,
/// preambles and ASTs) for opened files.
/// TUScheduler is not thread-safe, only one thread should be providing updates
/// and scheduling tasks.
/// Callbacks are run on a threadpool and it's appropriate to do slow work in
/// them. Each task has a name, used for tracing (should be UpperCamelCase).
/// FIXME(sammccall): pull out a scheduler options struct.
class TUScheduler {
public:
  TUScheduler(unsigned AsyncThreadsCount, bool StorePreamblesInMemory,
              ASTParsedCallback ASTCallback,
              std::chrono::steady_clock::duration UpdateDebounce);
  ~TUScheduler();

  /// Returns estimated memory usage for each of the currently open files.
  /// The order of results is unspecified.
  std::vector<std::pair<Path, std::size_t>> getUsedBytesPerFile() const;

  /// Schedule an update for \p File. Adds \p File to a list of tracked files if
  /// \p File was not part of it before.
  /// FIXME(ibiryukov): remove the callback from this function.
  void update(PathRef File, ParseInputs Inputs, WantDiagnostics WD,
              UniqueFunction<void(std::vector<Diag>)> OnUpdated);

  /// Remove \p File from the list of tracked files and schedule removal of its
  /// resources.
  void remove(PathRef File);

  /// Schedule an async read of the AST. \p Action will be called when AST is
  /// ready. The AST passed to \p Action refers to the version of \p File
  /// tracked at the time of the call, even if new updates are received before
  /// \p Action is executed.
  /// If an error occurs during processing, it is forwarded to the \p Action
  /// callback.
  void runWithAST(llvm::StringRef Name, PathRef File,
                  Callback<InputsAndAST> Action);

  /// Schedule an async read of the Preamble.
  /// The preamble may be stale, generated from an older version of the file.
  /// Reading from locations in the preamble may cause the files to be re-read.
  /// This gives callers two options:
  /// - validate that the preamble is still valid, and only use it in this case
  /// - accept that preamble contents may be outdated, and try to avoid reading
  ///   source code from headers.
  /// If an error occurs during processing, it is forwarded to the \p Action
  /// callback.
  void runWithPreamble(llvm::StringRef Name, PathRef File,
                       Callback<InputsAndPreamble> Action);

  /// Wait until there are no scheduled or running tasks.
  /// Mostly useful for synchronizing tests.
  bool blockUntilIdle(Deadline D) const;

private:
  /// This class stores per-file data in the Files map.
  struct FileData;

  const bool StorePreamblesInMemory;
  const std::shared_ptr<PCHContainerOperations> PCHOps;
  const ASTParsedCallback ASTCallback;
  Semaphore Barrier;
  llvm::StringMap<std::unique_ptr<FileData>> Files;
  // None when running tasks synchronously and non-None when running tasks
  // asynchronously.
  llvm::Optional<AsyncTaskRunner> PreambleTasks;
  llvm::Optional<AsyncTaskRunner> WorkerThreads;
  std::chrono::steady_clock::duration UpdateDebounce;
};
} // namespace clangd
} // namespace clang

#endif
