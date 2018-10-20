set(BITYPE_SOURCES
  bitype.cpp
  )

include_directories(..)

set(BITYPE_CFLAGS ${SANITIZER_COMMON_CFLAGS})

add_custom_target(bitype)

add_compiler_rt_runtime(clang_rt.bitype
            STATIC
            ARCHS x86_64
            SOURCES ${BITYPE_SOURCES}
            CFLAGS ${BITYPE_CFLAGS}
            PARENT_TARGET bitype)

add_sanitizer_rt_symbols(clang_rt.bitype)

add_dependencies(compiler-rt bitype)