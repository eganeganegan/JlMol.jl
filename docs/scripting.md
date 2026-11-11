# Julia scripting

The docked console evaluates real Julia source in a persistent `JlMolSession` module. Variables and function definitions remain available between executions. Standard output, standard error, results, and stack traces are displayed in the console.

```julia
using JlMol
protein = loadstructure(joinpath(pkgdir(JlMol), "resources", "1CRN.pdb"))
atom_count(protein)
setrepresentation!(protein, :cartoon)
colorby!(protein, :secondary_structure)
selected_atom()
```

Click an atom in the viewport and run `selected_atom()` again to read its identity. Use `active_structure()` to work with a structure already open in the GUI.

Supported representations are `:ball_and_stick`, `:spacefill`, `:points`, `:lines`, `:c_alpha_trace`, `:backbone`, `:cartoon`, and `:ribbon`. Supported color schemes are `:element`, `:chain`, `:residue`, `:secondary_structure`, and `:sequence`.

Use `setatomcolors!(protein, rgb)` to apply your own palette as a `3 × N` matrix with values in `[0, 1]` in topology atom order. Use `colorby!` to switch back to a built-in scheme. See [Julia plugins](plugins.md) for registering reusable coloring methods, commands, and analyses in the desktop menus.

`setopacity!(protein, 0.45)` sets the same per-structure opacity as the GUI. `opacity(protein)` reads it. Values must be finite and in `[0, 1]`; 0 hides the geometry and 1 is fully opaque. `setvisible!` leaves opacity unchanged. Omit the structure argument to use the active object.

`ActiveStructure` stores a stable native object ID. Loading another file does not invalidate existing handles. A handle becomes invalid only after `close!` removes its object.

Measurements use stable atom IDs and return angstrom or degree values:

```julia
distance(protein, 1, 2)          # angstroms
angle(protein, 1, 2, 3)         # degrees, atom 2 is the vertex
dihedral(protein, 1, 2, 3, 4)   # signed degrees
radius_of_gyration(protein)      # mass-weighted angstroms
```

The structure argument may be omitted from `distance`, `angle`, and `dihedral` to use the active structure. Coincident positions produce a Julia error for angles; coincident or collinear positions produce an error for dihedrals.

`snapshot(mol)` returns owned `3 × N` coordinates, stable atom IDs, identity keys, and atom names. `alignstructures(mobile, reference)` returns a fitted rigid transform, aligned coordinate copy, atom correspondence, and RMSD. `rmsd(mobile, reference)` fits by default; `align=false` measures raw displacement. Structure comparisons match atom identities, or accept an explicit `atom_pairs` mapping for subsets.

`contactmap(mol; cutoff=8.0, atom_name="CA")` returns a symmetric contact matrix with ordered axis IDs. All of these analyses use nonperiodic coordinates. See [scientific methods](scientific_methods.md) for examples, numerical conventions, and size limits.

Use `attachtrajectory!(mol, path)` with a matching topology, `setframe!(mol, frame)` to seek, and `trajectory_info(mol)` to inspect the current frame. `trajectory_frame(mol, frame)`, `trajectory_rmsd(mol)`, `trajectory_rmsf(mol)`, and `trajectory_pca(mol)` read frames without moving the viewport. The analysis functions accept `stride` and `atom_ids`. Pass PCA scores to `cluster_conformations` and use the returned representative indices to look up original frame numbers. See [trajectories](trajectories.md) for linked plots, periodic cell data, and explicit minimum-image distances.

The embedded session executes local Julia code with the app's permissions. Computation runs synchronously on the GUI thread, so long-running expressions pause interaction.

## Script editor

The Julia Script Editor dock provides multiple movable tabs, line numbers, Julia syntax highlighting, UTF-8 open/save/save-as, unsaved-change prompts, search/replace, and output display. `Ctrl+Return` runs the selection, or the whole file when no text is selected. `Ctrl+Shift+Return` always runs the whole file. Editor and console execution share the same persistent `JlMolSession` module.

In the console, Up and Down recall command history for single-line input.

Run the bundled example from the console or open it in the editor. It loads crambin, calculates raw RMSD for a three-frame water trajectory, and leaves the protein active.

```julia
using JlMol
include(joinpath(pkgdir(JlMol), "examples", "quickstart.jl"))
```

See the [quickstart script](../examples/quickstart.jl) and [Julia API reference](api.md).

