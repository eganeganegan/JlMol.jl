# Save and reopen a workspace

Choose **File > Save Workspace** to write a `.jlmol` file. Use **File > Open Workspace** to replace the current molecular scene with the saved one.

A workspace restores the loaded structures, active structure, visibility, representations, opacity, built-in and custom colors, and ordered atom selection. It also restores the camera, projection, background, lighting, label mode, dock layout, and each attached trajectory's frame. Playback opens paused at the saved speed.

The workspace restores trajectory-analysis settings and recalculates a saved plot using the referenced frames. A contact map is recalculated at its original frame, including when the viewer is on a different frame. A displayed structure comparison is recalculated with the saved reference object. If a calculation fails, the workspace still opens and JlMol reports the failure.

## Move a workspace

The versioned JSON file links to the original topology and trajectory files. Put those files beside the workspace or in subfolders if you want to move the project as a folder. JlMol stores those paths relative to the workspace and uses absolute paths for inputs elsewhere. Keep the original input files and topology atom order for reproducible results.

If an input is missing or unreadable, JlMol reports the path and keeps the current scene. Move the data back to that location or update the path in the workspace JSON before retrying. Workspace saves replace the destination atomically. Julia console variables, scripts, and loaded plugin modules belong to the app session. An applied custom palette travels with the workspace as RGB values.

## Verify the round trip

From the repository root after building on macOS, run the native workspace check.

```sh
JULIA_DEPOT_PATH="$PWD/build/julia-depot" ./build/jlmol.app/Contents/MacOS/jlmol \
  test/data/water.xyz --workspace-test test/data/water_analysis.xyz
```
