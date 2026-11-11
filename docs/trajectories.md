# Molecular dynamics trajectories

1. Open the topology structure with **File > Open Structure**. Select it in the Object Explorer if several structures are loaded.
2. Choose **File > Attach Trajectory** and select an XTC, TRR, DCD, or multi-frame XYZ file.
3. Use the **Trajectory** dock to play, pause, or seek. Drag the timeline or enter a one-based frame number. The playback speed sets the display rate in frames per second, independent of simulated time. Playback pauses at the final frame; press Play again to restart.

The trajectory must have the same atom count **and order** as the structure. The reader checks counts on attachment and as frames are read. Coordinate-only formats cannot establish atom correspondence, so the topology must come from the same simulation. Each displayed frame needs atom positions. A TRR record that contains only velocities or forces is rejected. Attaching replaces any previous trajectory on that object and displays its first frame. A failed attach or seek preserves the current scene. Frame changes preserve atom IDs, topology bonds, selection, representation, and opacity.

The loader keeps the trajectory open and reads frames on demand. Each trajectory caches up to eight recently used frames, with a 32 MiB cap on cached coordinate storage. During scrubbing, the viewer keeps showing the last loaded frame while it reads the latest requested frame in the background. Playback waits for each frame to load, so the displayed rate may fall below the selected rate. Large files can take time to index on attachment. Use **View > Fit to Structure** to adjust the camera as needed.

## Linked analysis

With a trajectory attached, open **Analysis > Trajectory Analysis**, choose **RMSD**, **RMSF**, or **PCA**, and click **Calculate**.

- RMSD measures displacement relative to the reference frame. Click the plot to seek to the nearest sampled frame. The marker follows the timeline and playback, even when the current frame was not sampled.
- RMSF measures each atom's positional fluctuation about its mean over the sampled frames. Click a point to select that atom. Selections made in the structure appear as plot markers.
- PCA plots each sampled frame on the first two principal components and shows the fraction of variance explained by each axis. Click a point to seek to that frame. The marker follows playback on sampled frames. For optional cluster coloring, set **Clusters** to 2–8 before calculating. The groups describe proximity in the PC1/PC2 projection.
- Enable **Selected atoms** before calculating to use only the current selection. Once calculated in this mode, the plot updates after subsequent selection changes. If you want to keep the full RMSF plot while inspecting individual atoms, leave **Selected atoms** off.

**Align** removes rigid translation and rotation by fitting each frame to **Ref**, the one-based reference frame. The same atom set is used for the fit and the measurement. A fit needs at least three non-collinear atoms; turn **Align** off to analyze a single atom or to include rigid motion. **Stride** controls frame sampling: stride 10 uses frames 1, 11, 21, and so on. Hover over a point for its frame or atom ID and value.

The GUI suggests a stride for large trajectories. RMSD and RMSF allow up to 5,000 sampled frames, and RMSF plots support up to 20,000 atoms. PCA allows up to 500 sampled frames and two million analyzed atom-frame records. All three calculations limit the number of atom-frame records read to 20 million. Calculations run synchronously and use the stored, nonperiodic coordinates, so unwrap split molecules before analyzing them.

Use **Analysis > Contact Map** to calculate contacts for the displayed frame. Click a cell to select its two atoms, or select atoms in the viewport to highlight their rows and columns. The map remains tied to its original frame as you seek or play, and its summary shows both frame numbers when they differ. See [scientific methods](scientific_methods.md) for cutoff and map size details.

## Julia

```julia
using JlMol
mol = loadstructure("topology.pdb")
attachtrajectory!(mol, "simulation.xtc")
trajectory_info(mol)                       # frames, frame, time_ps, cell
setframe!(mol, 10)                          # display frame 10
positions = trajectory_frame(mol, 20)      # read coordinates without seeking
rmsd_by_frame = trajectory_rmsd(mol; reference=1)
fluctuation = trajectory_rmsf(mol; reference=1, stride=10)
# fluctuation.atom_ids and fluctuation.values map each measurement to an atom
pca = trajectory_pca(mol; reference=1, stride=10)
clusters = cluster_conformations(pca.scores; k=2)
representative_frames = pca.frames[clusters.representatives]
```

Frame numbers are one-based in Julia and the GUI, zero-based in the C ABI. Position matrices are `3 × N` in angstroms, in topology order. XTC and TRR provide time in picoseconds. DCD and XYZ time is reported as unavailable. A periodic cell, when provided, is a `3 × 3` matrix whose columns are the basis vectors in angstroms. Frame coordinates, including coordinates outside the cell, are used as stored.

The bundled reader preserves rotated orthogonal boxes as full cell matrices. Its axis-aligned fast path still expects a along x, b along y, and c along z. Rotated boxes use the general matrix path so the cell stays in the same coordinate system as the atoms.

`periodic_distance(mol, id1, id2)` explicitly applies minimum-image geometry to atom IDs in the displayed frame. `minimum_image_displacement(delta, cell)` provides the same operation on coordinate vectors. These methods require an orthogonal cell and reject skewed cells. Triclinic cell metadata is preserved for other analyses. Automatic wrapping, molecule unwrapping, and periodic contact maps are not implemented. Ordinary measurements and RMSD remain nonperiodic; unwrap split molecules before using them. By default trajectory RMSD removes translation and rotation using the existing Kabsch implementation, which requires at least three non-collinear points. Use `align=false` for raw RMSD.

## Reproduce the playback check

From the repository root after building, open the water topology and attach its three-frame XYZ trajectory in the GUI. To run the native timeline and framebuffer check on macOS:

```sh
JULIA_DEPOT_PATH="$PWD/build/julia-depot" ./build/jlmol.app/Contents/MacOS/jlmol \
  test/data/water.xyz --trajectory-test test/data/water_trajectory.xyz
```

CTest also generates XTC, TRR, and DCD trajectories and verifies random access, cell data, cache eviction, and failed-seek behavior.

To try the linked plots, open `test/data/water.xyz` and attach `test/data/water_analysis.xyz`. Turn **Align** off to see the oxygen move while both hydrogens stay still. Use stride 1 to see all three frames on the PCA scatter. The native integration check exercises this example:

```sh
JULIA_DEPOT_PATH="$PWD/build/julia-depot" ./build/jlmol.app/Contents/MacOS/jlmol \
  test/data/water.xyz --linked-analysis-test test/data/water_analysis.xyz
```
