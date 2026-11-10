# Building JlMol

Requirements: CMake 3.24+, Ninja, Git, a C++20 compiler, Qt 6 Base (Widgets and OpenGLWidgets), zlib, and Julia 1.10 or newer. CMake fetches pinned Chemfiles 0.10.4 and applies the rotated-cell import patch. Set `JLMOL_USE_SYSTEM_CHEMFILES=ON` to use an installed package instead; a system build without that patch may reject rotated orthogonal boxes. Set `JLMOL_USE_CHEMFILES=OFF` for the PDB-only fallback build.

On Apple Silicon with Homebrew:

```sh
brew install cmake ninja qtbase zlib juliaup
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
JULIA_DEPOT_PATH="$PWD/build/julia-depot" JLMOL_CORE_LIBRARY="$PWD/build/libjlmol_core.dylib" julia --project=. test/runtests.jl
open build/jlmol.app
```

Set `CMAKE_PREFIX_PATH` to the appropriate Qt installation on other systems. CMake locates Julia through the `julia` executable and embeds an rpath for the development build.

See [packaging](packaging.md) for building and verifying the standalone macOS app and the status of Windows and Linux packaging.

