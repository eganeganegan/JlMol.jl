# Building JlMol

Requirements: CMake 3.24+, Ninja, a C++20 compiler, Qt 6 Base (Widgets and OpenGLWidgets), and Julia 1.10 or newer.

On Apple Silicon with Homebrew:

```sh
brew install cmake ninja qtbase juliaup
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
JULIA_DEPOT_PATH="$PWD/build/julia-depot" JLMOL_CORE_LIBRARY="$PWD/build/libjlmol_core.dylib" julia --project=. test/runtests.jl
open build/jlmol.app
```

Set `CMAKE_PREFIX_PATH` to the appropriate Qt installation on other systems. CMake locates Julia through the `julia` executable and embeds an rpath for the development build.

