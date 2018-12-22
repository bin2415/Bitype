# Bitype

"Mapping to Bits: Efficiently Detecting Type Confusion Errors" is presented in ACSAC 2018.

## Build

### Create a build directory

```
mkdir builddir
cd builddir
```

### build it

```
cmake DCMAKE_BUILD_TYPE=Release path/to/llvm/source/root
make -j$(nproc)
```

## Preprocess

We use clang tool to collect the class inheritance relationship, so we need configure the compilation database to the tool, there are some methods to get the compilation database, the tutorial link is [JSON Compilation Database Format Specification](https://clang.llvm.org/docs/JSONCompilationDatabase.html).

### CMake

For CMake, there is a config CMAKE_EXPORT_COMPILE_COMMANDS=ON to generate the compilation database

### Others

Use the tool [Bear](https://github.com/rizsotto/Bear).

### Collect the Information

```
path/to/builddir/bin/find-class-decls -p path/to/compilation/database path/to/project
```

## Bitype harden

```
path/to/clang++ -fsanitize=bitype -mllvm -bitype-codemap=path/to/coding-num.txt -mllvm -bitype-castrelated=path/to/castrelated-set.txt -mllvm -bitype-inheritance=path/to/safecast.txt -mllvm -bitype-debug-file=path/to/downcastLoc.txt -mllvm -handle-placement-new -mllvm -handle-reinterpret-cast path/to/the/source/code -o path/to/executable
```

The -handle-placement-new and -handle-reinterpret-cast configs are the optimization configs. We reference them from [Hextype](https://github.com/HexHive/HexType)
