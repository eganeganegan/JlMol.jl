# Performance benchmark

Build JlMol and run the benchmark in a native OpenGL window. On macOS, run from the repository root.

```sh
cmake --preset dev
cmake --build --preset dev -j 8
JULIA_DEPOT_PATH="$PWD/build/julia-depot" ./build/jlmol.app/Contents/MacOS/jlmol --benchmark
```

The benchmark generates temporary structures with 1,000, 10,000, and 100,000 atoms, arranged on a grid with explicit bonds and ten atoms per residue. Each structure has eight XYZ frames. The benchmark fixes the initial window size, isolates GUI settings, and reports the actual viewport dimensions and device scale. It uses ball-and-stick rendering and reports tab-separated results.

| Operation | What it measures |
| --- | --- |
| `open`, `attach` | One structure import or trajectory attachment, followed by a redraw |
| `idle` | Redraw with the scene unchanged |
| `color` | Toggle element and chain coloring |
| `pick` | Click near the viewport center and update the selection and panels |
| `select_group` | Select 100 atoms at 1,000 atoms, or 500 at the larger sizes |
| `play_uncached` | Seek and redraw seven sequential frames on their first read |
| `play_cached` | Seek and redraw seven frames from the decoded frame cache |

The update column covers synchronous command and panel work. Geometry is CPU preparation of the vertex data. Upload brackets the buffer calls with OpenGL synchronization and reports the maximum number of bytes submitted for an operation. Redraw includes geometry, upload, rendering, and framebuffer readback. The total column is the median of per-sample update and redraw sums. All repeated operations use seven samples. Playback rows measure the work done per frame before any timer delay.

The asynchronous seek summary measures the same seven uncached frames through the timeline. It reports the median time the GUI spends submitting a seek and the median elapsed time until the scene publishes the frame.

## Before and after

Measured with the Debug build on arm64 macOS 27.0.1, with a 659 × 320 logical-pixel viewport at 2× device scale. Each cell gives total milliseconds before → after for the same workload.

| Atoms | Pick | Group selection | Color change | Cached frame | Uncached XYZ frame |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 3.60 → 2.26 | 3.94 → 2.15 | 3.56 → 2.22 | 1.85 → 1.84 | 3.18 → 3.06 |
| 10,000 | 21.21 → 3.34 | 37.33 → 2.42 | 20.65 → 3.74 | 4.42 → 2.84 | 16.67 → 15.29 |
| 100,000 | 256.27 → 11.87 | 433.67 → 3.73 | 243.07 → 16.97 | 24.49 → 8.45 | 150.29 → 138.27 |

At 100,000 atoms, a pick uploaded at most 72 bytes after the change, down from 7.92 MB. The measured upload phase was 0.18 → 0.52 ms, including synchronization and driver overhead. Cached frame uploads still submit 7.92 MB and took about 0.18 ms in both runs. Most of the cached-frame gain came from CPU geometry preparation, which fell from 13.61 to 3.04 ms. Structure import and its first redraw took about 0.79 seconds in both runs.

## Current limits

The timeline reads frames and prepares molecule snapshots on a worker. Text decoding still dominates the latency of large XYZ frames. The cache holds up to eight decoded frames within a 32 MiB coordinate budget. The GUI thread updates the viewport and panels after each frame is ready. With a visible Analysis Workspace, radius of gyration is recalculated for each new snapshot.

The viewport batches atoms, bonds, and ribbon geometry in shared buffers. Picks project visible atoms individually. Style changes and changes to ribbon or trace coordinates rebuild geometry. Ordinary atom and bond frame updates reuse the existing CPU geometry and regenerate positions. Full-buffer updates refresh the GPU buffer storage, and localized changes write the affected vertex ranges. Bond inference for structure files without explicit bonds can take quadratic time.
