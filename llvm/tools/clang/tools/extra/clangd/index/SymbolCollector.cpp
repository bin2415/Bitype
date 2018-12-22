//===--- SymbolCollector.cpp -------------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SymbolCollector.h"
#include "../AST.h"
#include "../CodeCompletionStrings.h"
#include "../Logger.h"
#include "../URI.h"
#include "CanonicalIncludes.h"
#include "clang/AST/DeclCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Index/IndexSymbol.h"
#include "clang/Index/USRGeneration.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"

namespace clang {
namespace clangd {

namespace {
// Returns a URI of \p Path. Firstly, this makes the \p Path absolute using the
// current working directory of the given SourceManager if the Path is not an
// absolute path. If failed, this resolves relative paths against \p FallbackDir
// to get an absolute path. Then, this tries creating an URI for the absolute
// path with schemes specified in \p Opts. This returns an URI with the first
// working scheme, if there is any; otherwise, this returns None.
//
// The Path can be a path relative to the build directory, or retrieved from
// the SourceManager.
llvm::Optional<std::string> toURI(const SourceManager &SM, StringRef Path,
                                  const SymbolCollector::Options &Opts) {
  llvm::SmallString<128> AbsolutePath(Path);
  if (std::error_code EC =
          SM.getFileManager().getVirtualFileSystem()->makeAbsolute(
              AbsolutePath))
    llvm::errs() << "Warning: could not make absolute file: '" << EC.message()
                 << '\n';
  if (llvm::sys::path::is_absolute(AbsolutePath)) {
    // Handle the symbolic link path case where the current working directory
    // (getCurrentWorkingDirectory) is a symlink./ We always want to the real
    // file path (instead of the symlink path) for the  C++ symbols.
    //
    // Consider the following example:
    //
    //   src dir: /project/src/foo.h
    //   current working directory (symlink): /tmp/build -> /project/src/
    //
    // The file path of Symbol is "/project/src/foo.h" instead of
    // "/tmp/build/foo.h"
    if (const DirectoryEntry *Dir = SM.getFileManager().getDirectory(
            llvm::sys::path::parent_path(AbsolutePath.str()))) {
      StringRef DirName = SM.getFileManager().getCanonicalName(Dir);
      SmallString<128> AbsoluteFilename;
      llvm::sys::path::append(AbsoluteFilename, DirName,
                              llvm::sys::path::filename(AbsolutePath.str()));
      AbsolutePath = AbsoluteFilename;
    }
  } else if (!Opts.FallbackDir.empty()) {
    llvm::sys::fs::make_absolute(Opts.FallbackDir, AbsolutePath);
    llvm::sys::path::remove_dots(AbsolutePath, /*remove_dot_dot=*/true);
  }

  std::string ErrMsg;
  for (const auto &Scheme : Opts.URISchemes) {
    auto U = URI::create(AbsolutePath, Scheme);
    if (U)
      return U->toString();
    ErrMsg += llvm::toString(U.takeError()) + "\n";
  }
  log(llvm::Twine("Failed to create an URI for file ") + AbsolutePath + ": " +
      ErrMsg);
  return llvm::None;
}

// "a::b::c", return {"a::b::", "c"}. Scope is empty if there's no qualifier.
std::pair<llvm::StringRef, llvm::StringRef>
splitQualifiedName(llvm::StringRef QName) {
  assert(!QName.startswith("::") && "Qualified names should not start with ::");
  size_t Pos = QName.rfind("::");
  if (Pos == llvm::StringRef::npos)
    return {StringRef(), QName};
  return {QName.substr(0, Pos + 2), QName.substr(Pos + 2)};
}

bool shouldFilterDecl(const NamedDecl *ND, ASTContext *ASTCtx,
                      const SymbolCollector::Options &Opts) {
  using namespace clang::ast_matchers;
  if (ND->isImplicit())
    return true;
  // Skip anonymous declarations, e.g (anonymous enum/class/struct).
  if (ND->getDeclName().isEmpty())
    return true;

  // FIXME: figure out a way to handle internal linkage symbols (e.g. static
  // variables, function) defined in the .cc files. Also we skip the symbols
  // in anonymous namespace as the qualifier names of these symbols are like
  // `foo::<anonymous>::bar`, which need a special handling.
  // In real world projects, we have a relatively large set of header files
  // that define static variables (like "static const int A = 1;"), we still
  // want to collect these symbols, although they cause potential ODR
  // violations.
  if (ND->isInAnonymousNamespace())
    return true;

  // We only want:
  //   * symbols in namespaces or translation unit scopes (e.g. no class
  //     members)
  //   * enum constants in unscoped enum decl (e.g. "red" in "enum {red};")
  auto InTopLevelScope = hasDeclContext(
      anyOf(namespaceDecl(), translationUnitDecl(), linkageSpecDecl()));
  // Don't index template specializations.
  auto IsSpecialization =
      anyOf(functionDecl(isExplicitTemplateSpecialization()),
            cxxRecordDecl(isExplicitTemplateSpecialization()),
            varDecl(isExplicitTemplateSpecialization()));
  if (match(decl(allOf(unless(isExpansionInMainFile()),
                       anyOf(InTopLevelScope,
                             hasDeclContext(enumDecl(InTopLevelScope,
                                                     unless(isScoped())))),
                       unless(IsSpecialization))),
            *ND, *ASTCtx)
          .empty())
    return true;

  return false;
}

// We only collect #include paths for symbols that are suitable for global code
// completion, except for namespaces since #include path for a namespace is hard
// to define.
bool shouldCollectIncludePath(index::SymbolKind Kind) {
  using SK = index::SymbolKind;
  switch (Kind) {
  case SK::Macro:
  case SK::Enum:
  case SK::Struct:
  case SK::Class:
  case SK::Union:
  case SK::TypeAlias:
  case SK::Using:
  case SK::Function:
  case SK::Variable:
  case SK::EnumConstant:
    return true;
  default:
    return false;
  }
}

/// Gets a canonical include (URI of the header or <header>  or "header") for
/// header of \p Loc.
/// Returns None if fails to get include header for \p Loc.
/// FIXME: we should handle .inc files whose symbols are expected be exported by
/// their containing headers.
llvm::Optional<std::string>
getIncludeHeader(llvm::StringRef QName, const SourceManager &SM,
                 SourceLocation Loc, const SymbolCollector::Options &Opts) {
  llvm::StringRef FilePath = SM.getFilename(Loc);
  if (FilePath.empty())
    return llvm::None;
  if (Opts.Includes) {
    llvm::StringRef Mapped = Opts.Includes->mapHeader(FilePath, QName);
    if (Mapped != FilePath)
      return (Mapped.startswith("<") || Mapped.startswith("\""))
                 ? Mapped.str()
                 : ("\"" + Mapped + "\"").str();
  }

  return toURI(SM, SM.getFilename(Loc), Opts);
}

// Return the symbol location of the given declaration `D`.
//
// For symbols defined inside macros:
//   * use expansion location, if the symbol is formed via macro concatenation.
//   * use spelling location, otherwise.
llvm::Optional<SymbolLocation> getSymbolLocation(
    const NamedDecl &D, SourceManager &SM, const SymbolCollector::Options &Opts,
    const clang::LangOptions &LangOpts, std::string &FileURIStorage) {
  SourceLocation NameLoc = findNameLoc(&D);
  auto U = toURI(SM, SM.getFilename(NameLoc), Opts);
  if (!U)
    return llvm::None;
  FileURIStorage = std::move(*U);
  SymbolLocation Result;
  Result.FileURI = FileURIStorage;
  Result.StartOffset = SM.getFileOffset(NameLoc);
  Result.EndOffset = Result.StartOffset + clang::Lexer::MeasureTokenLength(
                                              NameLoc, SM, LangOpts);
  return std::move(Result);
}

// Checks whether \p ND is a definition of a TagDecl (class/struct/enum/union)
// in a header file, in which case clangd would prefer to use ND as a canonical
// declaration.
// FIXME: handle symbol types that are not TagDecl (e.g. functions), if using
// the the first seen declaration as canonical declaration is not a good enough
// heuristic.
bool isPreferredDeclaration(const NamedDecl &ND, index::SymbolRoleSet Roles) {
  using namespace clang::ast_matchers;
  return (Roles & static_cast<unsigned>(index::SymbolRole::Definition)) &&
         llvm::isa<TagDecl>(&ND) &&
         match(decl(isExpansionInMainFile()), ND, ND.getASTContext()).empty();
}

} // namespace

SymbolCollector::SymbolCollector(Options Opts) : Opts(std::move(Opts)) {}

void SymbolCollector::initialize(ASTContext &Ctx) {
  ASTCtx = &Ctx;
  CompletionAllocator = std::make_shared<GlobalCodeCompletionAllocator>();
  CompletionTUInfo =
      llvm::make_unique<CodeCompletionTUInfo>(CompletionAllocator);
}

// Always return true to continue indexing.
bool SymbolCollector::handleDeclOccurence(
    const Decl *D, index::SymbolRoleSet Roles,
    ArrayRef<index::SymbolRelation> Relations, FileID FID, unsigned Offset,
    index::IndexDataConsumer::ASTNodeInfo ASTNode) {
  assert(ASTCtx && PP.get() && "ASTContext and Preprocessor must be set.");
  assert(CompletionAllocator && CompletionTUInfo);
  const NamedDecl *ND = llvm::dyn_cast<NamedDecl>(D);
  if (!ND)
    return true;

  // Mark D as referenced if this is a reference coming from the main file.
  // D may not be an interesting symbol, but it's cheaper to check at the end.
  if (Opts.CountReferences &&
      (Roles & static_cast<unsigned>(index::SymbolRole::Reference)) &&
      ASTCtx->getSourceManager().getMainFileID() == FID)
    ReferencedDecls.insert(ND);

  // Don't continue indexing if this is a mere reference.
  if (!(Roles & static_cast<unsigned>(index::SymbolRole::Declaration) ||
        Roles & static_cast<unsigned>(index::SymbolRole::Definition)))
    return true;
  if (shouldFilterDecl(ND, ASTCtx, Opts))
    return true;

  llvm::SmallString<128> USR;
  if (index::generateUSRForDecl(ND, USR))
    return true;
  SymbolID ID(USR);

  const NamedDecl &OriginalDecl = *cast<NamedDecl>(ASTNode.OrigD);
  const Symbol *BasicSymbol = Symbols.find(ID);
  if (!BasicSymbol) // Regardless of role, ND is the canonical declaration.
    BasicSymbol = addDeclaration(*ND, std::move(ID));
  else if (isPreferredDeclaration(OriginalDecl, Roles))
    // If OriginalDecl is preferred, replace the existing canonical
    // declaration (e.g. a class forward declaration). There should be at most
    // one duplicate as we expect to see only one preferred declaration per
    // TU, because in practice they are definitions.
    BasicSymbol = addDeclaration(OriginalDecl, std::move(ID));

  if (Roles & static_cast<unsigned>(index::SymbolRole::Definition))
    addDefinition(OriginalDecl, *BasicSymbol);
  return true;
}

void SymbolCollector::finish() {
  // At the end of the TU, add 1 to the refcount of the ReferencedDecls.
  for (const auto *ND : ReferencedDecls) {
    llvm::SmallString<128> USR;
    if (!index::generateUSRForDecl(ND, USR))
      if (const auto *S = Symbols.find(SymbolID(USR))) {
        Symbol Inc = *S;
        ++Inc.References;
        Symbols.insert(Inc);
      }
  }
  ReferencedDecls.clear();
}

const Symbol *SymbolCollector::addDeclaration(const NamedDecl &ND,
                                              SymbolID ID) {
  auto &SM = ND.getASTContext().getSourceManager();

  std::string QName;
  llvm::raw_string_ostream OS(QName);
  PrintingPolicy Policy(ASTCtx->getLangOpts());
  // Note that inline namespaces are treated as transparent scopes. This
  // reflects the way they're most commonly used for lookup. Ideally we'd
  // include them, but at query time it's hard to find all the inline
  // namespaces to query: the preamble doesn't have a dedicated list.
  Policy.SuppressUnwrittenScope = true;
  ND.printQualifiedName(OS, Policy);
  OS.flush();

  Symbol S;
  S.ID = std::move(ID);
  std::tie(S.Scope, S.Name) = splitQualifiedName(QName);
  S.SymInfo = index::getSymbolInfo(&ND);
  std::string FileURI;
  if (auto DeclLoc =
          getSymbolLocation(ND, SM, Opts, ASTCtx->getLangOpts(), FileURI))
    S.CanonicalDeclaration = *DeclLoc;

  // Add completion info.
  // FIXME: we may want to choose a different redecl, or combine from several.
  assert(ASTCtx && PP.get() && "ASTContext and Preprocessor must be set.");
  CodeCompletionResult SymbolCompletion(&ND, 0);
  const auto *CCS = SymbolCompletion.CreateCodeCompletionString(
      *ASTCtx, *PP, CodeCompletionContext::CCC_Name, *CompletionAllocator,
      *CompletionTUInfo,
      /*IncludeBriefComments*/ true);
  std::string Label;
  std::string SnippetInsertText;
  std::string IgnoredLabel;
  std::string PlainInsertText;
  getLabelAndInsertText(*CCS, &Label, &SnippetInsertText,
                        /*EnableSnippets=*/true);
  getLabelAndInsertText(*CCS, &IgnoredLabel, &PlainInsertText,
                        /*EnableSnippets=*/false);
  std::string FilterText = getFilterText(*CCS);
  std::string Documentation = getDocumentation(*CCS);
  std::string CompletionDetail = getDetail(*CCS);

  std::string Include;
  if (Opts.CollectIncludePath && shouldCollectIncludePath(S.SymInfo.Kind)) {
    // Use the expansion location to get the #include header since this is
    // where the symbol is exposed.
    if (auto Header = getIncludeHeader(
            QName, SM, SM.getExpansionLoc(ND.getLocation()), Opts))
      Include = std::move(*Header);
  }
  S.CompletionFilterText = FilterText;
  S.CompletionLabel = Label;
  S.CompletionPlainInsertText = PlainInsertText;
  S.CompletionSnippetInsertText = SnippetInsertText;
  Symbol::Details Detail;
  Detail.Documentation = Documentation;
  Detail.CompletionDetail = CompletionDetail;
  Detail.IncludeHeader = Include;
  S.Detail = &Detail;

  Symbols.insert(S);
  return Symbols.find(S.ID);
}

void SymbolCollector::addDefinition(const NamedDecl &ND,
                                    const Symbol &DeclSym) {
  if (DeclSym.Definition)
    return;
  // If we saw some forward declaration, we end up copying the symbol.
  // This is not ideal, but avoids duplicating the "is this a definition" check
  // in clang::index. We should only see one definition.
  Symbol S = DeclSym;
  std::string FileURI;
  if (auto DefLoc = getSymbolLocation(ND, ND.getASTContext().getSourceManager(),
                                      Opts, ASTCtx->getLangOpts(), FileURI))
    S.Definition = *DefLoc;
  Symbols.insert(S);
}

} // namespace clangd
} // namespace clang
