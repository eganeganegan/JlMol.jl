# Architecture

JlMol is split into a Qt desktop shell, an OpenGL viewport, a native molecular core, a C ABI, and a Julia package/runtime.

## Authoritative state

`jlmol::MolecularScene` is the sole authority for loaded objects, the active object, per-object visibility and style, and selected atoms. Object IDs remain stable until their object is closed. Selections store an object ID and atom ID, which avoids collisions between structures. The GUI's command dispatcher and the Julia C wrappers both mutate that same scene. The Julia module deliberately exposes `ActiveStructure`, which contains an integer handle rather than a native pointer.

The bridge registers a scene-change callback. Julia commands call the C ABI, the C ABI validates and mutates the scene, and the callback queues viewport and panel refreshes on Qt's GUI thread. Julia evaluation currently runs synchronously on the GUI thread; this is safe for the runtime and scene, but long-running work will temporarily block interaction. A later worker design must preserve Julia's thread-registration and Qt affinity requirements.

The console and script editor share one embedded `JlMolSession` module, so imports, variables, functions, and molecular handles persist across both interfaces. Script documents remain Qt-owned text and are only passed to Julia when the user explicitly runs a selection or file.

## Rendering

The initial renderer targets OpenGL 4.1 core, the newest OpenGL baseline available on macOS. Atoms are batched point-sprite sphere impostors and bonds are batched line geometry. A per-vertex scale allows objects with different representations to share the atom draw call. GPU resources use Qt RAII wrappers and are destroyed while the widget context is current. Molecular data contains no OpenGL types, leaving room for a future Qt RHI, Metal, or Vulkan device implementation. OpenGL is deprecated by Apple, so it is a compatibility backend rather than the permanent macOS strategy.

Protein traces use chain-aware C-alpha or N-CA-C connectivity with geometric break detection. Cartoon and ribbon centerlines use Catmull-Rom interpolation over contiguous C-alpha segments. An indexed mesh shares cross-section rings and smooth normals between neighboring segments and caps only the ends of each trace. Cartoon profiles form flattened helix bands, widened and tapered beta-strand arrowheads, and round coil tubes. Local frames use parallel transport with a curvature guide for helix faces. PDB HELIX/SHEET records and mmCIF helix/strand ranges become atom annotations and provide red helix, yellow sheet, and blue coil coloring. Chemfiles imports coordinates and topology; a separate metadata reader maps mmCIF annotation endpoints into its label residue namespace. Camera motion changes only the view transform, with quaternion orbit and eased zoom.

Per-object opacity is shared scene state. The viewport records each object's vertex ranges in its shared buffers. Translucent draws test against opaque depth and blend into color-accumulation and coverage textures before compositing to the Qt framebuffer. This avoids sorting intersecting objects and leaves fully opaque scenes on the existing path. See [rendering](rendering.md) for controls, limitations, and framebuffer tests.

## Coordinates, formats, and analysis

Chemfiles 0.10.4 provides the primary structure reader. Static structure import loads the first frame, atom and residue metadata, explicit bond orders when available, and inferred bonds otherwise. PDB, mmCIF, XYZ, SDF, MOL2, GRO, and the other formats built into Chemfiles share this path. The original PDB parser remains as a dependency-free fallback when Chemfiles is disabled. Coordinates, distances, and radius-of-gyration results are expressed in ångströms; angles and dihedrals are expressed in degrees. Radius of gyration is mass-weighted about the center of mass using standard atomic weights, with carbon mass as the fallback for an unknown element.

A [trajectory](trajectories.md) attaches to one scene object and uses its atom order and bonds. A serialized Chemfiles reader supplies random access and a bounded cache of coordinate frames. Desktop attachment indexes the file and prepares the first frame on a worker. The GUI thread installs it on the original object if the request is current and the previous trajectory is still attached. Desktop seeks prepare molecule snapshots on a separate worker and publish them only if the original snapshot and trajectory are still current. The dispatcher keeps one active read and the latest pending request for each operation. Playback updates the viewport and frame-dependent panels without rebuilding the object tree. Julia frame access and attachment, and workspace restoration, are synchronous. The viewer does not apply periodic wrapping implicitly.

The Trajectory Analysis dock evaluates RMSD, RMSF, and PCA in an isolated embedded Julia module and copies typed values into a Qt chart. The chart sends frame indices and atom IDs through the same dispatcher as the timeline and viewport. Frame changes update the RMSD or PCA marker without recomputing the series. Selection-scoped calculations update after a short debounce, and switching objects or replacing the trajectory invalidates the result. The contact-map widget uses stable atom IDs for selection and retains the frame at which the matrix was calculated. Switching the active object or its attached trajectory clears the map.

The native analysis layer owns small geometry kernels that operate directly on application coordinates. It clamps inverse-cosine input and rejects coincident or collinear configurations instead of returning invalid angles. The GUI inspector, analysis workspace, C ABI, and Julia package share these kernels. Higher-level rigid alignment, atom correspondence, RMSD, and contact maps are implemented in Julia using the standard `LinearAlgebra` library. The GUI calls those same Julia functions. Analysis returns owned coordinate copies and never duplicates authoritative scene state or borrows mutable native buffers. See `scientific_methods.md` for conventions, mapping, and limits.

## Plugin extensions

Julia owns the registry of locally loaded plugins and their callbacks. Each file is included in a separate module namespace. Registration stays local until its initializer succeeds. The Qt menus request typed metadata and invoke actions by plugin and command ID. File paths and labels cross that boundary as strings. Plugin analysis results use the existing Analysis Workspace. Custom colors cross the native ABI as a validated, copied RGB array and remain attached to the scene object through trajectory frame changes. Rendering reads that array directly.

## Workspaces

The desktop writes a versioned JSON description with input file references, project-local object indices, camera state, analysis settings, and the Qt dock layout. It opens the input files and builds a staged scene before touching the live scene. After validating the dock state, it commits the staged objects into the existing scene and assigns fresh object IDs. This preserves the native bridge connection and prevents stale Julia handles from resolving to a different molecule. Built-in analysis results are regenerated from saved settings. Custom color buffers are copied as RGB values.

## Figure export

The viewport renders to a dedicated OpenGL framebuffer at the export size. The export shares the scene projection and geometry passes, including premultiplied-alpha composition, and paints labels at the final pixel size. The SVG writer keeps projected labels in a separate text layer. Animated PNG reads frames from the attached trajectory and substitutes each frame's coordinates for the active object's render, leaving the live scene and timeline unchanged. An atomic file write commits only after the last frame succeeds.

## Incremental updates

Object Explorer children stay attached to their stable object IDs across frame, selection, and style changes. The viewport keeps the last rendered scene state and compares it with the next generation. Changes to atom highlights write only the affected vertex ranges, and ordinary trajectory steps update the positions in existing batches. Changes to topology, representation, palette, or coordinate-dependent traces and ribbons use the full rebuild path. Animation exports that temporarily substitute coordinates invalidate that render state before the viewport draws again.

## Native ABI

`native/bridge/JlMolAPI.h` defines ABI version 7, which adds per-atom RGB colors in topology order. The ABI also supports trajectory attachment, seek, frame metadata, and coordinate reads without seeking. Strings are UTF-8 and returned string pointers are thread-local and valid until the next string-returning call on the same thread. The application owns the scene and molecular objects; Julia closes objects by stable ID and never destroys native pointers. Measurement calls accept stable object and atom IDs and write validated results through explicit output pointers. The snapshot ABI copies xyz triples and IDs into caller-owned, capacity-checked arrays; indexed identity fields supply matching metadata. Snapshots are acquired synchronously on the GUI thread, where scene mutation and Julia evaluation are serialized. Background mutation will require an atomic snapshot protocol before it is enabled.

