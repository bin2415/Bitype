///===-- Representation.h - ClangDoc Represenation --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the internal representations of different declaration
// types for the clang-doc tool.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_DOC_REPRESENTATION_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_DOC_REPRESENTATION_H

#include "clang/AST/Type.h"
#include "clang/Basic/Specifiers.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include <array>
#include <string>

namespace clang {
namespace doc {

using SymbolID = std::array<uint8_t, 20>;

struct Info;
enum class InfoType {
  IT_namespace,
  IT_record,
  IT_function,
  IT_enum,
  IT_default
};

// A representation of a parsed comment.
struct CommentInfo {
  CommentInfo() = default;
  CommentInfo(CommentInfo &&Other) : Children(std::move(Other.Children)) {}

  SmallString<16>
      Kind; // Kind of comment (TextComment, InlineCommandComment,
            // HTMLStartTagComment, HTMLEndTagComment, BlockCommandComment,
            // ParamCommandComment, TParamCommandComment, VerbatimBlockComment,
            // VerbatimBlockLineComment, VerbatimLineComment).
  SmallString<64> Text;      // Text of the comment.
  SmallString<16> Name;      // Name of the comment (for Verbatim and HTML).
  SmallString<8> Direction;  // Parameter direction (for (T)ParamCommand).
  SmallString<16> ParamName; // Parameter name (for (T)ParamCommand).
  SmallString<16> CloseName; // Closing tag name (for VerbatimBlock).
  bool SelfClosing = false;  // Indicates if tag is self-closing (for HTML).
  bool Explicit = false; // Indicates if the direction of a param is explicit
                         // (for (T)ParamCommand).
  llvm::SmallVector<SmallString<16>, 4>
      AttrKeys; // List of attribute keys (for HTML).
  llvm::SmallVector<SmallString<16>, 4>
      AttrValues; // List of attribute values for each key (for HTML).
  llvm::SmallVector<SmallString<16>, 4>
      Args; // List of arguments to commands (for InlineCommand).
  std::vector<std::unique_ptr<CommentInfo>>
      Children; // List of child comments for this CommentInfo.
};

struct Reference {
  Reference() = default;
  Reference(llvm::StringRef Name) : UnresolvedName(Name) {}
  Reference(SymbolID USR, InfoType IT) : USR(USR), RefType(IT) {}

  SymbolID USR;                   // Unique identifer for referenced decl
  SmallString<16> UnresolvedName; // Name of unresolved type.
  InfoType RefType =
      InfoType::IT_default; // Indicates the type of this Reference (namespace,
                            // record, function, enum, default).
};

// A base struct for TypeInfos
struct TypeInfo {
  TypeInfo() = default;
  TypeInfo(SymbolID &Type, InfoType IT) : Type(Type, IT) {}
  TypeInfo(llvm::StringRef RefName) : Type(RefName) {}

  Reference Type; // Referenced type in this info.
};

// Info for field types.
struct FieldTypeInfo : public TypeInfo {
  FieldTypeInfo() = default;
  FieldTypeInfo(SymbolID &Type, InfoType IT, llvm::StringRef Name)
      : TypeInfo(Type, IT), Name(Name) {}
  FieldTypeInfo(llvm::StringRef RefName, llvm::StringRef Name)
      : TypeInfo(RefName), Name(Name) {}

  SmallString<16> Name; // Name associated with this info.
};

// Info for member types.
struct MemberTypeInfo : public FieldTypeInfo {
  MemberTypeInfo() = default;
  MemberTypeInfo(SymbolID &Type, InfoType IT, llvm::StringRef Name)
      : FieldTypeInfo(Type, IT, Name) {}
  MemberTypeInfo(llvm::StringRef RefName, llvm::StringRef Name)
      : FieldTypeInfo(RefName, Name) {}

  AccessSpecifier Access =
      clang::AccessSpecifier::AS_none; // Access level associated with this
                                       // info (public, protected, private,
                                       // none).
};

struct Location {
  Location() = default;
  Location(int LineNumber, SmallString<16> Filename)
      : LineNumber(LineNumber), Filename(std::move(Filename)) {}

  int LineNumber;           // Line number of this Location.
  SmallString<32> Filename; // File for this Location.
};

/// A base struct for Infos.
struct Info {
  Info() = default;
  Info(Info &&Other) : Description(std::move(Other.Description)) {}
  virtual ~Info() = default;

  SymbolID USR; // Unique identifier for the decl described by this Info.
  SmallString<16> Name; // Unqualified name of the decl.
  llvm::SmallVector<Reference, 4>
      Namespace; // List of parent namespaces for this decl.
  std::vector<CommentInfo> Description; // Comment description of this decl.
};

// Info for namespaces.
struct NamespaceInfo : public Info {};

// Info for symbols.
struct SymbolInfo : public Info {
  llvm::Optional<Location> DefLoc;    // Location where this decl is defined.
  llvm::SmallVector<Location, 2> Loc; // Locations where this decl is declared.
};

// TODO: Expand to allow for documenting templating and default args.
// Info for functions.
struct FunctionInfo : public SymbolInfo {
  bool IsMethod = false; // Indicates whether this function is a class method.
  Reference Parent;      // Reference to the parent class decl for this method.
  TypeInfo ReturnType;   // Info about the return type of this function.
  llvm::SmallVector<FieldTypeInfo, 4> Params; // List of parameters.
  AccessSpecifier Access =
      AccessSpecifier::AS_none; // Access level for this method (public,
                                // private, protected, none).
};

// TODO: Expand to allow for documenting templating, inheritance access,
// friend classes
// Info for types.
struct RecordInfo : public SymbolInfo {
  TagTypeKind TagType = TagTypeKind::TTK_Struct; // Type of this record (struct,
                                                 // class, union, interface).
  llvm::SmallVector<MemberTypeInfo, 4>
      Members;                             // List of info about record members.
  llvm::SmallVector<Reference, 4> Parents; // List of base/parent records (does
                                           // not include virtual parents).
  llvm::SmallVector<Reference, 4>
      VirtualParents; // List of virtual base/parent records.
};

// TODO: Expand to allow for documenting templating.
// Info for types.
struct EnumInfo : public SymbolInfo {
  bool Scoped =
      false; // Indicates whether this enum is scoped (e.g. enum class).
  llvm::SmallVector<SmallString<16>, 4> Members; // List of enum members.
};

// TODO: Add functionality to include separate markdown pages.

} // namespace doc
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_DOC_REPRESENTATION_H
