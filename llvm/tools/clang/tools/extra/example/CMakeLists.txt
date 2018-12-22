set(LLVM_LINK_COMPONENTS Support)

add_clang_executable(Example
  Example.cpp
)

target_link_libraries(Example
  PRIVATE
  clangTooling
  clangBasic
  clangASTMatchers
  clangAST
  clangBasic
  clangFrontend
  clangRewriteFrontend
)
