# JlMol.jl

**A Julia-Powered Molecular Visualization Workstation**

JlMol is a native molecular viewer built with C++20, Qt 6 Widgets, OpenGL, and an embedded Julia runtime. The native scene is authoritative: GUI actions and Julia commands operate on the same molecules, representation, color scheme, and atom selection.

## Current vertical slice

- Native dockable Qt workstation shell
- PDB loading from the GUI or by drag-and-drop
- OpenGL atom and inferred-bond rendering
- Orbit, pan, zoom, perspective/orthographic projection, and atom picking
- Object hierarchy and selected-atom inspector
- Persistent embedded Julia console with captured output and stack traces
- Julia commands for querying and changing the active native scene

## Build and launch (macOS/Homebrew)

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
JULIA_DEPOT_PATH="$PWD/build/julia-depot" JLMOL_CORE_LIBRARY="$PWD/build/libjlmol_core.dylib" julia --project=. test/runtests.jl
open build/jlmol.app
```

From a build tree, the executable is also available at `build/jlmol.app/Contents/MacOS/jlmol`.

In the Julia console, try:

```julia
using JlMol
mol = active_structure()
atom_count(mol)
setrepresentation!(mol, :spacefill)
colorby!(mol, :chain)
selected_atom()
```

See the [architecture guide](docs/architecture.md) for scene ownership and thread boundaries.

