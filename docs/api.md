# Julia API reference

Run `using JlMol` inside the desktop console or script editor. Scene methods operate on the same objects as the viewport. Matrix-based analyses accept Julia arrays without a desktop scene. Coordinate matrices are `3 × N` with one atom per column and positions in angstroms.

## Structures and appearance

| API | Behavior |
| --- | --- |
| `ActiveStructure`, `active_structure()`, `structures()` | A stable native object handle, the active handle, or a vector of loaded handles. The active handle has ID zero when the scene is empty. |
| `loadstructure(path)`, `activate!(mol)`, `close!(mol)` | Load a file and make it active, activate an existing object, or close it. Closing invalidates its handle. |
| `name(mol)`, `atom_count(mol)`, `bond_count(mol)` | Read the display name and topology counts. Omit `mol` to use the active object. |
| `isvisible(mol)`, `setvisible!(mol, visible)`, `opacity(mol)`, `setopacity!(mol, value)` | Read or set visibility and opacity independently. Opacity takes a finite value in `[0, 1]`. |
| `representation(mol)`, `setrepresentation!(mol, style)`, `color_scheme(mol)`, `colorby!(mol, scheme)` | Read or set the style and built-in color scheme. See the [scripting guide](scripting.md) for supported symbols. |
| `setatomcolors!(mol, rgb)` | Apply a `3 × N` RGB matrix in topology order. Channels must be finite and in `[0, 1]`. |
| `selected_atom()` | Return the selected atom's object ID, stable atom ID, serial, name, element, residue name, residue ID, and chain. Returns `nothing` when no atom is selected. |

`isvisible`, `opacity`, `representation`, and `color_scheme` read the active object when called without an argument. `setopacity!`, `setrepresentation!`, and `colorby!` also accept the value alone for the active object.

## Measurements and coordinate analysis

| API | Behavior |
| --- | --- |
| `distance(mol, a, b)`, `angle(mol, a, b, c)`, `dihedral(mol, a, b, c, d)` | Measure by stable atom ID. Distances are in angstroms and angles in degrees. The second ID is the angle vertex. Dihedrals are signed. |
| `radius_of_gyration(mol)` | Return the mass-weighted radius of gyration in angstroms. |
| `StructureSnapshot`, `snapshot(mol)` | Copy the object's `coordinates`, `atom_ids`, `atom_keys`, and `atom_names`. |
| `AlignmentResult`, `alignstructures(mobile, reference; atom_pairs=nothing)` | Return `rotation`, `translation`, fitted `coordinates`, `rmsd`, `mobile_ids`, and `reference_ids`. The structure and snapshot methods match atom identities or accept explicit ID pairs. Matrices are matched by column and take no `atom_pairs` argument. |
| `rmsd(mobile, reference; align=true, atom_pairs=nothing)` | Return displacement in angstroms for two structures or snapshots. Pass `align=false` for raw displacement. The matrix method accepts `align` and compares corresponding columns. |
| `contactmap(mol; cutoff=8.0, atom_name=nothing)` | Return `contacts`, ordered `atom_ids`, and `cutoff`. Accepts a structure or snapshot. The matrix overload accepts `cutoff` and returns a `BitMatrix`. |

The measurements, radius of gyration, snapshot, and structure contact map use the active object if `mol` is omitted. Alignment and contact maps use nonperiodic coordinates. The [scientific methods guide](scientific_methods.md) explains atom matching, limits, and validation.

## Trajectories and periodic geometry

| API | Behavior |
| --- | --- |
| `attachtrajectory!(mol, path)`, `setframe!(mol, frame)` | Attach matching coordinates and display frame 1, or seek to a one-based frame. |
| `trajectory_info(mol)`, `trajectory_frame(mol, frame)` | Return `frames`, `frame`, `time_ps`, and `cell`, or copy one frame's coordinates without seeking. Time and cell are `nothing` when unavailable. `trajectory_info` returns `nothing` before a trajectory is attached. |
| `trajectory_rmsd(mol; reference=1, align=true, stride=1, atom_ids=nothing)` | Return one RMSD per sampled frame, in angstroms. |
| `trajectory_rmsf(mol; reference=1, align=true, stride=1, atom_ids=nothing)` | Return `atom_ids` and RMSF `values` in angstroms. |
| `trajectory_pca(mol; reference=1, align=true, stride=1, atom_ids=nothing, components=2)` | Return `scores`, `loadings`, `mean_coordinates`, `explained_variance`, `explained_fraction`, `frames`, and `atom_ids`. Scores have one column per sampled frame. |
| `cluster_conformations(scores; k=2, max_iterations=100)` | Group the columns of a score matrix. Returns `assignments`, `centers`, `representatives` as column indices, and `converged`. |
| `minimum_image_displacement(delta, cell)`, `periodic_distance(mol, a, b)` | Apply the minimum-image convention to a displacement vector or a pair of stable atom IDs in the displayed frame. Both accept orthogonal cells, including rotated orthogonal cells. Cell basis vectors are the columns of a `3 × 3` matrix. |

Omit `mol` from the trajectory methods and `periodic_distance` to use the active object. Trajectory analysis reads frames without seeking and uses stored coordinates. See the [trajectory guide](trajectories.md) for format, time, and periodic conventions.

## Plugins

| API | Behavior |
| --- | --- |
| `PluginMetadata(id, name, version; api_version=PLUGIN_API_VERSION, description="")` | Declare a plugin's metadata and minimum compatible API version. `PLUGIN_API_VERSION` is `v"1.0.0"`. |
| `loadplugin!(path)`, `plugins()`, `enableplugin!(id, enabled)` | Load a local plugin, list metadata and enabled state, or toggle its menu actions in this session. |
| `register_command!(registration, id, label, callback)`, `register_analysis!(registration, id, label, callback)`, `register_coloring!(registration, id, label, callback)` | Register callbacks from the plugin initializer. |
| `run_plugin_command!(plugin_id, command_id)` | Run an enabled plugin action, using the active structure for analysis and coloring. |

For initializer signatures and callback results, see the [plugin guide](plugins.md). The lower-level C ABI and ownership rules are documented in the [native bridge header](../native/bridge/JlMolAPI.h) and [architecture guide](architecture.md#native-abi).
