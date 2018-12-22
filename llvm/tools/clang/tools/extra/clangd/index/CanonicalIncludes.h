//===-- CanonicalIncludes.h - remap #include header -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// At indexing time, we decide which file to #included for a symbol.
// Usually this is the file with the canonical decl, but there are exceptions:
// - private headers may have pragmas pointing to the matching public header.
//   (These are "IWYU" pragmas, named after the include-what-you-use tool).
// - the standard library is implemented in many files, without any pragmas.
//   We have a lookup table for common standard library implementations.
//   libstdc++ puts char_traits in bits/char_traits.h, but we #include <string>.
//
//===---------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANGD_INDEX_CANONICALINCLUDES_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANGD_INDEX_CANONICALINCLUDES_H

#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Regex.h"
#include <mutex>
#include <string>
#include <vector>

namespace clang {
namespace clangd {

/// Maps a definition location onto an #include file, based on a set of filename
/// rules.
/// Only const methods (i.e. mapHeader) in this class are thread safe.
class CanonicalIncludes {
public:
  CanonicalIncludes() = default;

  /// Adds a string-to-string mapping from \p Path to \p CanonicalPath.
  void addMapping(llvm::StringRef Path, llvm::StringRef CanonicalPath);

  /// Maps all files matching \p RE to \p CanonicalPath
  void addRegexMapping(llvm::StringRef RE, llvm::StringRef CanonicalPath);

  /// Sets the canonical include for any symbol with \p QualifiedName.
  /// Symbol mappings take precedence over header mappings.
  void addSymbolMapping(llvm::StringRef QualifiedName,
                        llvm::StringRef CanonicalPath);

  /// Returns the canonical include for symbol with \p QualifiedName, which is
  /// declared in \p Header
  llvm::StringRef mapHeader(llvm::StringRef Header,
                            llvm::StringRef QualifiedName) const;

private:
  // A map from header patterns to header names. This needs to be mutable so
  // that we can match again a Regex in a const function member.
  // FIXME(ioeric): All the regexes we have so far are suffix matches. The
  // performance could be improved by allowing only suffix matches instead of
  // arbitrary regexes.
  mutable std::vector<std::pair<llvm::Regex, std::string>>
      RegexHeaderMappingTable;
  // A map from fully qualified symbol names to header names.
  llvm::StringMap<std::string> SymbolMapping;
  // Guards Regex matching as it's not thread-safe.
  mutable std::mutex RegexMutex;
};

/// Returns a CommentHandler that parses pragma comment on include files to
/// determine when we should include a different header from the header that
/// directly defines a symbol. Mappinps are registered with \p Includes.
///
/// Currently it only supports IWYU private pragma:
/// https://github.com/include-what-you-use/include-what-you-use/blob/master/docs/IWYUPragmas.md#iwyu-pragma-private
std::unique_ptr<CommentHandler>
collectIWYUHeaderMaps(CanonicalIncludes *Includes);

/// Adds mapping for system headers and some special symbols (e.g. STL symbols
/// in <iosfwd> need to be mapped individually). Approximately, the following
/// system headers are handled:
///   - C++ standard library e.g. bits/basic_string.h$ -> <string>
///   - Posix library e.g. bits/pthreadtypes.h$ -> <pthread.h>
///   - Compiler extensions, e.g. include/avx512bwintrin.h$ -> <immintrin.h>
/// The mapping is hardcoded and hand-maintained, so it might not cover all
/// headers.
void addSystemHeadersMapping(CanonicalIncludes *Includes);

} // namespace clangd
} // namespace clang

#endif //LLVM_CLANG_TOOLS_EXTRA_CLANGD_INDEX_HEADERMAPCOLLECTOR_H
