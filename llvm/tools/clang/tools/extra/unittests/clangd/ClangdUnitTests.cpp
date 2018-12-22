//===-- ClangdUnitTests.cpp - ClangdUnit tests ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Annotations.h"
#include "ClangdUnit.h"
#include "TestFS.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/PCHContainerOperations.h"
#include "clang/Frontend/Utils.h"
#include "llvm/Support/ScopedPrinter.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace clang {
namespace clangd {
using namespace llvm;

namespace {
using testing::ElementsAre;
using testing::Field;
using testing::IsEmpty;
using testing::Pair;

testing::Matcher<const Diag &> WithFix(testing::Matcher<Fix> FixMatcher) {
  return Field(&Diag::Fixes, ElementsAre(FixMatcher));
}

testing::Matcher<const Diag &> WithNote(testing::Matcher<Note> NoteMatcher) {
  return Field(&Diag::Notes, ElementsAre(NoteMatcher));
}

// FIXME: this is duplicated with FileIndexTests. Share it.
ParsedAST build(StringRef Code, std::vector<const char *> Flags = {}) {
  std::vector<const char *> Cmd = {"clang", "main.cpp"};
  Cmd.insert(Cmd.begin() + 1, Flags.begin(), Flags.end());
  auto CI = createInvocationFromCommandLine(Cmd);
  auto Buf = MemoryBuffer::getMemBuffer(Code);
  auto AST = ParsedAST::Build(std::move(CI), nullptr, std::move(Buf),
                              std::make_shared<PCHContainerOperations>(),
                              vfs::getRealFileSystem());
  assert(AST.hasValue());
  return std::move(*AST);
}

std::vector<Diag> buildDiags(llvm::StringRef Code,
                             std::vector<const char *> Flags = {}) {
  return build(Code, std::move(Flags)).getDiagnostics();
}

MATCHER_P2(Diag, Range, Message,
           "Diag at " + llvm::to_string(Range) + " = [" + Message + "]") {
  return arg.Range == Range && arg.Message == Message;
}

MATCHER_P3(Fix, Range, Replacement, Message,
           "Fix " + llvm::to_string(Range) + " => " +
               testing::PrintToString(Replacement) + " = [" + Message + "]") {
  return arg.Message == Message && arg.Edits.size() == 1 &&
         arg.Edits[0].range == Range && arg.Edits[0].newText == Replacement;
}

MATCHER_P(EqualToLSPDiag, LSPDiag,
          "LSP diagnostic " + llvm::to_string(LSPDiag)) {
  return std::tie(arg.range, arg.severity, arg.message) ==
         std::tie(LSPDiag.range, LSPDiag.severity, LSPDiag.message);
}

MATCHER_P(EqualToFix, Fix, "LSP fix " + llvm::to_string(Fix)) {
  if (arg.Message != Fix.Message)
    return false;
  if (arg.Edits.size() != Fix.Edits.size())
    return false;
  for (std::size_t I = 0; I < arg.Edits.size(); ++I) {
    if (arg.Edits[I].range != Fix.Edits[I].range ||
        arg.Edits[I].newText != Fix.Edits[I].newText)
      return false;
  }
  return true;
}

// Helper function to make tests shorter.
Position pos(int line, int character) {
  Position Res;
  Res.line = line;
  Res.character = character;
  return Res;
}

/// Matches diagnostic that has exactly one fix with the same range and message
/// as the diagnostic itself.
testing::Matcher<const clangd::Diag &> DiagWithEqualFix(clangd::Range Range,
                                                        std::string Replacement,
                                                        std::string Message) {
  return AllOf(Diag(Range, Message), WithFix(Fix(Range, Replacement, Message)));
}

TEST(DiagnosticsTest, DiagnosticRanges) {
  // Check we report correct ranges, including various edge-cases.
  Annotations Test(R"cpp(
    void $decl[[foo]]();
    int main() {
      $typo[[go\
o]]();
      foo()$semicolon[[]]
      $unk[[unknown]]();
    }
  )cpp");
  llvm::errs() << Test.code();
  EXPECT_THAT(
      buildDiags(Test.code()),
      ElementsAre(
          // This range spans lines.
          AllOf(DiagWithEqualFix(
                    Test.range("typo"), "foo",
                    "use of undeclared identifier 'goo'; did you mean 'foo'?"),
                // This is a pretty normal range.
                WithNote(Diag(Test.range("decl"), "'foo' declared here"))),
          // This range is zero-width, and at the end of a line.
          DiagWithEqualFix(Test.range("semicolon"), ";",
                           "expected ';' after expression"),
          // This range isn't provided by clang, we expand to the token.
          Diag(Test.range("unk"), "use of undeclared identifier 'unknown'")));
}

TEST(DiagnosticsTest, FlagsMatter) {
  Annotations Test("[[void]] main() {}");
  EXPECT_THAT(buildDiags(Test.code()),
              ElementsAre(DiagWithEqualFix(Test.range(), "int",
                                           "'main' must return 'int'")));
  // Same code built as C gets different diagnostics.
  EXPECT_THAT(
      buildDiags(Test.code(), {"-x", "c"}),
      ElementsAre(AllOf(
          Diag(Test.range(), "return type of 'main' is not 'int'"),
          WithFix(Fix(Test.range(), "int", "change return type to 'int'")))));
}

TEST(DiagnosticsTest, Preprocessor) {
  // This looks like a preamble, but there's an #else in the middle!
  // Check that:
  //  - the #else doesn't generate diagnostics (we had this bug)
  //  - we get diagnostics from the taken branch
  //  - we get no diagnostics from the not taken branch
  Annotations Test(R"cpp(
    #ifndef FOO
    #define FOO
      int a = [[b]];
    #else
      int x = y;
    #endif
    )cpp");
  EXPECT_THAT(
      buildDiags(Test.code()),
      ElementsAre(Diag(Test.range(), "use of undeclared identifier 'b'")));
}

TEST(DiagnosticsTest, ToLSP) {
  clangd::Diag D;
  D.Message = "something terrible happened";
  D.Range = {pos(1, 2), pos(3, 4)};
  D.InsideMainFile = true;
  D.Severity = DiagnosticsEngine::Error;
  D.File = "foo/bar/main.cpp";

  clangd::Note NoteInMain;
  NoteInMain.Message = "declared somewhere in the main file";
  NoteInMain.Range = {pos(5, 6), pos(7, 8)};
  NoteInMain.Severity = DiagnosticsEngine::Remark;
  NoteInMain.File = "../foo/bar/main.cpp";
  NoteInMain.InsideMainFile = true;
  D.Notes.push_back(NoteInMain);

  clangd::Note NoteInHeader;
  NoteInHeader.Message = "declared somewhere in the header file";
  NoteInHeader.Range = {pos(9, 10), pos(11, 12)};
  NoteInHeader.Severity = DiagnosticsEngine::Note;
  NoteInHeader.File = "../foo/baz/header.h";
  NoteInHeader.InsideMainFile = false;
  D.Notes.push_back(NoteInHeader);

  clangd::Fix F;
  F.Message = "do something";
  D.Fixes.push_back(F);

  auto MatchingLSP = [](const DiagBase &D, llvm::StringRef Message) {
    clangd::Diagnostic Res;
    Res.range = D.Range;
    Res.severity = getSeverity(D.Severity);
    Res.message = Message;
    return Res;
  };

  // Diagnostics should turn into these:
  clangd::Diagnostic MainLSP = MatchingLSP(D, R"(something terrible happened

main.cpp:6:7: remark: declared somewhere in the main file

../foo/baz/header.h:10:11:
note: declared somewhere in the header file)");

  clangd::Diagnostic NoteInMainLSP =
      MatchingLSP(NoteInMain, R"(declared somewhere in the main file

main.cpp:2:3: error: something terrible happened)");

  // Transform dianostics and check the results.
  std::vector<std::pair<clangd::Diagnostic, std::vector<clangd::Fix>>> LSPDiags;
  toLSPDiags(D, [&](clangd::Diagnostic LSPDiag,
                    llvm::ArrayRef<clangd::Fix> Fixes) {
    LSPDiags.push_back({std::move(LSPDiag),
                        std::vector<clangd::Fix>(Fixes.begin(), Fixes.end())});
  });

  EXPECT_THAT(
      LSPDiags,
      ElementsAre(Pair(EqualToLSPDiag(MainLSP), ElementsAre(EqualToFix(F))),
                  Pair(EqualToLSPDiag(NoteInMainLSP), IsEmpty())));
}

} // namespace
} // namespace clangd
} // namespace clang
