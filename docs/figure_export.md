# Export figures and animations

Compose the view in the molecular viewport. Use **View > Projection** and **View > Camera Presets** to set the camera. **View > Background** offers Dark, White, and a custom color. **View > Lighting** offers Standard, Soft, and Flat. Use **View > Labels** to show the names of selected atoms or residues. **Color > By Residue Sequence** applies a rainbow from the first to the last residue group in topology order. The background, lighting, camera, and color settings are saved in a workspace.

## Still figures

Choose **File > Export Figure**. Enter the desired pixel size. **Match viewport proportions** preserves the framing of the current view. With it off, changing the aspect ratio changes the horizontal framing. Select **Transparent background** for an alpha channel, or keep the current viewport background. Use **Include viewport labels** to control label visibility in the export.

- **PNG** renders at the requested size with 300 DPI print metadata. Use the pixel dimensions to set the final print size. For example, 2,400 pixels at 300 DPI prints at 8 inches.
- **SVG with editable labels** embeds the molecular render at the requested resolution and keeps labels as vector text. The embedded image preserves depth, shading, and transparency.

Exports support up to 8,192 pixels per side and 16,777,216 pixels total, subject to available GPU memory. The renderer uses twofold supersampling when the larger render fits within those limits.

## Trajectory animations

Select a visible structure with an attached trajectory, then choose **File > Export Animation**. Set the first and last frame, frame step, and output frames per second. The export uses the current camera and appearance for every frame. Other structures remain in view at their current coordinates. Playback pauses during export, and the viewport stays on its current frame.

  The output is a looping Animated PNG (`.png`) with up to 2,000 frames. It includes the first frame as a static preview for PNG readers that display a single frame. Cancelling an export preserves any previous file at the destination. [PNG specification](https://www.w3.org/TR/png-3/)

## Verify the exports

From the repository root after building on macOS, run the native export check.

```sh
JULIA_DEPOT_PATH="$PWD/build/julia-depot" ./build/jlmol.app/Contents/MacOS/jlmol \
  test/data/water.xyz --export-test test/data/water_analysis.xyz
```
