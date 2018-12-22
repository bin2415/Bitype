set(LLVM_LINK_COMPONENTS Support)

add_clang_executable(collect-relation
  collectRelation.cpp
)

target_link_libraries(collect-relation
  PRIVATE
  clangTooling
  clangBasic
  clangASTMatchers
  clangAST
  clangBasic
  clangFrontend
)
