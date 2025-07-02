[settings]
arch={{detect_api.detect_arch()}}
build_type=Release
compiler=clang
compiler.cppstd=20
compiler.libcxx=libc++
compiler.version=20
os=Linux

[conf]
tools.build:compiler_executables={"c": "/usr/bin/clang-20", "cpp": "/usr/bin/clang++-20"}
