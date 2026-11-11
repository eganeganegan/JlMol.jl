# Building JlMol

The included presets require CMake 3.25 or newer. Direct configuration commands support CMake 3.24 or newer. Other requirements are Ninja, Git, a C++20 compiler, Qt 6 Base (Widgets and OpenGLWidgets), zlib, and Julia 1.10 or newer. CMake fetches pinned Chemfiles 0.10.4 and applies the rotated-cell import patch. Set `JLMOL_USE_SYSTEM_CHEMFILES=ON` to use an installed package instead. A system build without that patch may reject rotated orthogonal boxes. Set `JLMOL_USE_CHEMFILES=OFF` for the PDB-only fallback build.

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

## Tests

Run CTest for native structure and trajectory tests and the embedded Julia scientific reference suite. From a custom build directory, run `ctest --test-dir <build-dir> --output-on-failure`. Add `-C Release` for a multi-configuration Release build.

Run the Julia unit suite with `julia --project=. test/runtests.jl`. Set `JLMOL_CORE_LIBRARY` to the absolute path of the compiled native library. The developer commands above set the path on macOS. Use `libjlmol_core.so` in the Linux build directory or `Release/jlmol_core.dll` for a Windows Release build. Set `JULIA_DEPOT_PATH` to a directory inside the build tree to isolate Julia's cache.

The numerical tests run without a display. Native graphics and trajectory checks need a window and OpenGL 4.1. See the [rendering checks](rendering.md), [trajectory checks](trajectories.md#reproduce-the-playback-check), and [packaging workflow](../.github/workflows/portable-packages.yml).

See the [packaging guide](packaging.md) for building and testing the macOS app, Windows installer, and Linux archive.

