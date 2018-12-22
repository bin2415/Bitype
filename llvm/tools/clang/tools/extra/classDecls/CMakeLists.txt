add_clang_executable(find-class-decls FindClassDecls.cpp)

target_link_libraries(find-class-decls
 PRIVATE
 clangTooling
 clangSema
 )