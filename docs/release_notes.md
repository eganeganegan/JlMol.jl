# JlMol 0.1.0

JlMol brings molecular visualization and an embedded Julia session into one desktop app. The viewport, console, and scripts share the same molecular scene.

- Render protein helices as ribbons and beta strands as arrows. Orbit with fractional pointer motion, zoom with a wheel or trackpad, and color by secondary structure or residue sequence.
- Open common structure formats through Chemfiles and play XTC, TRR, DCD, and multi-frame XYZ trajectories. Seek to an RMSD or PCA point, or select an atom from an RMSF plot or contact map.
- Use the Julia console and script editor to inspect and style structures, run numerical analyses, or register local commands and coloring methods with plugins.
- Save the scene and camera in a workspace. Export high-resolution PNG, SVG with editable labels, or animated PNG from a trajectory.

The package includes crambin, two short water trajectories, and a Julia quickstart. [User guide](user_guide.md) · [Julia API](api.md) · [Scientific validation](scientific_methods.md#numerical-validation)

Package checks cover Apple Silicon macOS, Windows Server 2022 x64 with Mesa software OpenGL, and Ubuntu 24.04 x86-64 with Mesa software OpenGL. The viewport needs OpenGL 4.1. The Linux archive uses the host's desktop libraries and Windows uses the host's OpenGL driver. [Package verification](packaging.md)

Large trajectory reads and Julia calls run synchronously. Periodic minimum-image distances require orthogonal cell vectors. Ordinary measurements and trajectory analyses use the stored coordinates, so unwrap split molecules before analysis. [Trajectory guide](trajectories.md)
