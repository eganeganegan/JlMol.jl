# JlMol.jl

**A Julia-Powered Molecular Visualization Workstation**

JlMol is a native molecular viewer built with C++20, Qt 6 Widgets, OpenGL, Chemfiles, and an embedded Julia runtime. The native scene is authoritative: GUI actions and Julia commands operate on the same molecular objects, representations, color schemes, opacity, visibility, and atom selection.

## Current vertical slice

- Native dockable Qt workstation shell
- Multi-format structure loading through Chemfiles, including PDB, mmCIF, XYZ, SDF, MOL2, and GRO
- OpenGL atom and inferred-bond rendering
- Orbit, pan, zoom, perspective/orthographic projection, and atom picking
- Multiple simultaneous structures with stable IDs, visibility controls, per-object styles, and close support
- Object hierarchy and selected-atom inspector
- Chain and residue selection, selected-atom and selected-residue labels, and Shift-click distance, angle, and dihedral measurement
- Mass-weighted radius of gyration in the dockable analysis workspace and Julia API
- Rigid structural alignment, RMSD with validated atom matching, and adjustable atom contact maps in the GUI and Julia
- XTC, TRR, DCD, and multi-frame XYZ trajectory playback with a timeline and Julia frame access
- Linked RMSD, RMSF, and PCA trajectory plots with optional cluster coloring, plus interactive atom contact maps
- Local Julia plugins for desktop commands, analyses, and per-atom coloring
- Save and reopen workspaces with the scene, camera, trajectory position, analysis settings, and panel layout
- High-resolution PNG, SVG with editable labels, and animated PNG export
- Persistent embedded Julia console with captured output and stack traces
- Dockable multi-tab Julia editor with line numbers, syntax highlighting, file operations, search/replace, and run selection/file
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
distance(mol, 1, 2)
angle(mol, 1, 2, 3)
dihedral(mol, 1, 2, 3, 4)
radius_of_gyration(mol)

water = loadstructure("water.xyz")
structures()
setvisible!(water, false)
activate!(mol)
close!(water)
```

Protein-focused representations also include `:c_alpha_trace`, `:backbone`, `:cartoon`, and `:ribbon`. PDB and mmCIF annotations drive secondary-structure coloring. Choose **Color > By Secondary Structure** to distinguish helices and sheets, or right-click an object and choose **Set Opacity** to see overlapping structures. See [rendering](docs/rendering.md) for details.

See the [architecture guide](docs/architecture.md) for scene ownership and thread boundaries.

Use **Analysis > Align and Compare Structures** to compare two loaded objects, or **Analysis > Contact Map** for a C-alpha or all-atom map. Calculations use Julia's standard linear algebra and return owned results. See [scientific methods](docs/scientific_methods.md) for atom matching, nonperiodic coordinate assumptions, and Julia examples.

To play a simulation, open its topology and use **File > Attach Trajectory**. See [trajectories](docs/trajectories.md) for playback controls, Julia examples, and periodic-boundary limits.

After attaching a trajectory, open **Analysis > Trajectory Analysis**. Click an RMSD or PCA point to seek to its frame, or click an RMSF point to select an atom. Enable **Selected atoms** to recalculate for the current molecular selection. In **Analysis > Contact Map**, click a matrix cell to select its atom pair.

Choose **Plugins > Load Local Plugin** to load the bundled [Structure Tools plugin](examples/plugins/StructureTools.jl) or your own Julia module. The [plugin guide](docs/plugins.md) shows how to add commands, analyses, and coloring methods.

Use **File > Save Workspace** to save your view and **File > Open Workspace** to restore it. See [workspaces](docs/workspaces.md) for how to keep the workspace and its input files portable.

Use **File > Export Figure** for a still image or **File > Export Animation** for a trajectory clip. See [figure export](docs/figure_export.md) for appearance controls, formats, and size limits.

Run `--benchmark` to measure the native viewer at 1,000, 10,000, and 100,000 atoms. See [performance](docs/performance.md) for the workload, timings, and current limits.

The native test suite checks scientific results against analytic cases and independent crambin references. See [scientific methods](docs/scientific_methods.md#numerical-validation) for the fixtures, tolerances, and regeneration steps.

For a standalone macOS build with embedded Qt and Julia, see the [packaging guide](docs/packaging.md).

