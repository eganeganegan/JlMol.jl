# Scientific methods

Structure analyses use the displayed frame. Coordinates and distances are in angstroms, angles are in degrees. Ordinary analysis uses nonperiodic coordinates, so unwrap split molecules before analysis. The [trajectory tools](trajectories.md) also provide per-frame RMSD, per-atom RMSF, coordinate PCA, and an explicit minimum-image distance for orthogonal cells.

## Rigid alignment and RMSD

`alignstructures(mobile, reference)` uses Julia's `LinearAlgebra.svd` to fit a Kabsch rotation and translation. Reflections are explicitly excluded. It requires at least three non-collinear matched atoms and rejects empty, unequal, or nonfinite coordinate arrays. Coplanar sets with rank two are supported.

Matrix inputs have shape `3 × N`; column `i` in the mobile array corresponds to column `i` in the reference. Structure inputs match the full atom identity: model, chain, residue number, residue name, atom name, and alternate location. Atom storage order may differ. Missing or duplicate identities cause an error, so the calculation cannot silently compare unrelated atoms. Residue insertion codes are not yet preserved by the importer: if this creates duplicate identities, use an explicit mapping. Automatic sequence alignment or matching across renumbered chains is not implemented.

For selected subsets or different numbering, supply one-to-one `(mobile_atom_id, reference_atom_id)` tuples using `atom_pairs`. Stable atom IDs are local to their structure and need not agree between structures. Mapping entries must exist and neither side may repeat an atom.

The result contains `rotation`, `translation`, `coordinates`, `rmsd`, `mobile_ids`, and `reference_ids`. Fitted coordinates correspond to the matched mobile atoms, in mapping order. The transform is `rotation * coordinates .+ translation`. The operation returns an owned coordinate copy and leaves loaded structures unchanged.

`rmsd(mobile, reference)` fits before calculating the square root of the mean squared displacement over matched atoms. Set `align=false` to measure the raw displacement using the same identity mapping. The calculation is unweighted. GUI **Analysis > Align and Compare Structures** reports matched count and RMSD before and after fitting; explicit subset mapping is available through Julia.

```julia
using JlMol
mobile, reference = structures()[1:2]
fit = alignstructures(mobile, reference)
fit.rmsd
fit.rotation
fit.translation
rmsd(mobile, reference; align=false)

# IDs can be inspected with snapshot(mobile).atom_ids.
fit = alignstructures(mobile, reference; atom_pairs=[(1, 8), (2, 9), (3, 10)])
```

## Contact maps

`contactmap(points; cutoff=8.0)` returns a symmetric `BitMatrix` for a `3 × N` coordinate array. A pair is in contact at distance less than or equal to the cutoff. The diagonal is false; bonded atoms and sequence neighbors are included. This is an atom distance map, not a hydrogen-bond or residue-minimum-distance analysis.

For native structures or snapshots, the result contains `contacts`, ordered `atom_ids` for both axes, and `cutoff`. Set `atom_name="CA"` for a C-alpha map. The GUI offers C-alpha or all-atom selection, an adjustable distance cutoff, and a colored contact matrix with its unique pair count. Hover over a cell to inspect the two atom IDs, or click it to select the pair in the viewport. Viewport selection highlights the corresponding rows and columns. The map records its calculation frame and stays on that frame during playback.

```julia
contacts = contactmap(active_structure(); cutoff=8.0, atom_name="CA")
contacts.atom_ids
contacts.contacts
```

Dense maps use quadratic time and quadratic bit storage. GUI maps are limited to 1,024 selected atoms; the Julia API accepts up to 20,000. Choose a subset for larger structures. GUI analysis is synchronous and may briefly block interaction.

## Other geometry

Distances, angles, dihedrals, and radius of gyration share the native geometry kernels. Angles with coincident vertices and dihedrals with coincident or collinear bonds are rejected. Radius of gyration is mass-weighted about the center of mass, using common atomic weights with carbon mass as the fallback for an unknown element.

For atoms A, B, C, and D, look from B toward C. The torsion is positive when the near bond rotates clockwise toward the far bond. Earlier JlMol builds returned the opposite sign. [IUPAC torsion-angle convention](https://goldbook.iupac.org/terms/view/T06406)

## Trajectory fluctuations

`trajectory_rmsf` calculates the square root of the mean squared displacement of each atom from its mean position, dividing by the number of sampled frames. With alignment enabled, it fits each frame to the specified reference before updating the mean and squared deviations. It accumulates them online, without holding every decoded frame in memory. The resulting values are in angstroms and are paired with stable atom IDs. Both trajectory RMSD and RMSF accept `atom_ids` and `stride` and leave the displayed frame unchanged.

## Principal components and clustering

`trajectory_pca` optionally fits each sampled frame to the reference, then centers each Cartesian coordinate over the sampled frames and applies singular value decomposition. It weights each coordinate equally. Scores are in angstroms and explained variances are the squared singular values divided by the sample count minus one. The result includes the sampled frame numbers, stable atom IDs, coordinate means, loadings, and fractions of total variance. It leaves the displayed frame unchanged.

`cluster_conformations` uses deterministic Euclidean k-means with farthest-first initialization. It reports assignments, centers, representative sample columns closest to their assigned centers, and whether it converged. The GUI clusters the two displayed components. This is an exploratory view because frames close together in that projection may differ along other components.

## Numerical validation

Run `ctest --preset dev --output-on-failure` after building. The `scientific_reference` test uses the native bridge and embedded Julia without a desktop window. It runs when Chemfiles and embedded Julia are available. It uses checked-in fixtures, so normal test runs work offline with the project dependencies.

- Native analytic cases cover a 13 Å distance, 0°, 90°, and 180° angles, signed ±90° torsions, and undefined coincident or collinear geometry. Julia analytic cases cover rigid and reflected fits, reordered atom identities, exact contact cutoff inclusion, RMSF, PCA, and cluster assignments.
- The real-structure cases use the bundled [crambin 1CRN coordinates](../resources/1CRN.pdb). Biopython supplies angle and torsion values, and SciPy supplies distances and the complete set of Cα contacts within 8 Å. [RCSB PDB entry](https://www.rcsb.org/structure/1CRN) · [Biopython geometry](https://biopython.org/docs/latest/api/Bio.PDB.vectors.html) · [SciPy pairwise distances](https://docs.scipy.org/doc/scipy/reference/generated/scipy.spatial.distance.cdist.html)
- The five-frame synthetic trajectory starts from the same 327 atoms, adds rigid motion and controlled deformation, and moves one atom across the periodic boundary in frame 4. A small Python XDR writer encodes coordinates and axis-aligned box lengths in nanometers and timestamps in picoseconds. The test checks random access, unit conversion, stable atom order, and the shortest periodic distance against enumerated lattice images. A second trajectory contains a velocity-only record and checks that analysis and seeking reject it while preserving the displayed frame. [GROMACS TRR implementation](https://github.com/gromacs/gromacs/blob/main/src/gromacs/fileio/trrio.cpp) · [GROMACS units](https://manual.gromacs.org/current/reference-manual/definitions.html#md-units)
- SciPy fits centered Cartesian coordinates for the rigid transforms. NumPy calculates raw and fitted RMSD and population RMSF over the stored frames. Tests check the full trajectory and a reordered subset with stride 2, and confirm that reads and calculations preserve the displayed frame. [SciPy rigid fit](https://docs.scipy.org/doc/scipy/reference/generated/scipy.spatial.transform.Rotation.align_vectors.html)

The independent reference suite uses absolute tolerances on individual values or matrix elements. PDB and TRR coordinates pass through single-precision storage, so checks on those inputs allow for coordinate rounding.

| Quantity | Acceptance tolerance |
| --- | --- |
| Analytic native distances and angles | `1e-12` Å or degrees |
| Imported distances, coordinates, cell elements, fitted translations, RMSD, and RMSF | `1e-5` Å |
| Imported bond angles and signed torsions | `2e-4` degrees |
| Fitted rotation matrix elements | `2e-7` |
| Fitted rotation determinant | `1e-12` from +1 |
| Contact membership, atom IDs, and the fixture's exactly representable timestamps | Exact |

The reference table and both trajectories can be regenerated with `python3 test/generate_scientific_reference.py`. Add `--check` to compare fixture bytes without rewriting them. The checked-in reference was generated with NumPy 2.1.3, SciPy 1.17.1, and Biopython 1.88. These libraries are required only for regeneration.
