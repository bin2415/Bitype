set(LLVM_LINK_COMPONENTS Support)

add_clang_executable(inheri-Relation
  inheriRelation.cpp
)

target_link_libraries(inheri-Relation
  PRIVATE
  clangTooling
  clangBasic
  clangASTMatchers
  clangAST
  clangBasic
  clangFrontend
)
