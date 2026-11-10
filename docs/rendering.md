# Molecular rendering

Choose **Representation > Cartoon** for broad alpha-helix ribbons, tapered beta-strand arrows, and rounded loops. **Ribbon** gives a continuous band of uniform width instead. Select **Color > By Secondary Structure** for red helices, yellow sheets, and blue coil or unannotated residues. Other color schemes also work without changing the shapes.

Drag with the left button to orbit and with the right or middle button to pan. The orbit preserves fractional pointer motion and rotates continuously through the poles. Wheel and precision trackpad zoom ease toward the requested scale. Use **View > Camera Presets** to change orientation and **View > Labels** to label the current selection.

Use **View > Background**, **View > Lighting**, and **Color > By Residue Sequence** to style the viewport for a figure. The [export guide](figure_export.md) covers PNG, SVG with editable labels, and trajectory animation.

The importer reads PDB HELIX/SHEET records and helix and strand ranges from uncompressed `.cif` or `.mmcif` files. In mmCIF, label chain and sequence IDs are preferred. Author IDs, including insertion codes, are resolved to label IDs when a range lacks label endpoints. Ambiguous author IDs, incomplete ranges, and cross-chain ranges are skipped. The importer does not infer secondary structure when annotations are absent.

Right-click a structure in the Object Explorer and choose **Set Opacity**, or use **Representation > Set Active Structure Opacity**. At 0% the geometry, labels, and picking targets are hidden. The visibility checkbox is independent, so toggling it preserves opacity. The Julia equivalent is `setopacity!(mol, 0.45)`.

Fully opaque scenes use the existing multisampled rendering path. Mixed scenes render opaque geometry to a shared depth buffer, then blend translucent fragments using weighted color accumulation and accumulated coverage. Opaque surfaces occlude translucent fragments behind them. Blending is independent of object order up to floating-point rounding, but the color of many overlapping translucent layers is an approximation. The mixed pass uses single-sample render targets and can show less smooth edges than the opaque path. If the GPU cannot allocate the targets, the viewport logs a warning and uses opaque drawing.

To verify rendering on a native OpenGL window on macOS, run these commands from the repository root:

```sh
./build/jlmol.app/Contents/MacOS/jlmol resources/1CRN.pdb --graphics-test
./build/jlmol.app/Contents/MacOS/jlmol test/data/secondary.mmcif --graphics-test
```

The test compares framebuffer pixels for opaque and translucent representations, checks for rendered helix and sheet colors, reverses translucent draw order, and checks opaque depth. It also checks fractional orbit and an intermediate frame during trackpad zoom. Set `JLMOL_GRAPHICS_SNAPSHOT` to a new PNG path to save the resulting cartoon view.
