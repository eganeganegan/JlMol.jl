# Julia scripting

The docked console evaluates real Julia source in a persistent `JlMolSession` module. Variables and function definitions remain available between executions. Standard output, standard error, results, and stack traces are displayed in the console.

```julia
using JlMol
protein = active_structure()
atom_count(protein)
setrepresentation!(protein, :spacefill)
colorby!(protein, :chain)
selected_atom()

ligand = loadstructure("ligand.sdf")
structures()
setvisible!(ligand, false)
activate!(protein)
close!(ligand)
```

Supported representations are `:ball_and_stick`, `:spacefill`, `:points`, `:lines`, `:c_alpha_trace`, `:backbone`, `:cartoon`, and `:ribbon`. Supported color schemes are `:element`, `:chain`, `:residue`, and `:secondary_structure`.

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

The embedded session executes trusted local Julia code. It is not a security sandbox. Computation is synchronous in the current milestone; avoid long-running expressions during interactive work.

## Script editor

The Julia Script Editor dock provides multiple movable tabs, line numbers, Julia syntax highlighting, UTF-8 open/save/save-as, unsaved-change prompts, search/replace, and output display. `Ctrl+Return` runs the selection, or the whole file when no text is selected. `Ctrl+Shift+Return` always runs the whole file. Editor and console execution share the same persistent `JlMolSession` module.

In the console, Up and Down recall command history for single-line input.

