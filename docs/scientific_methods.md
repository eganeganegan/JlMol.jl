# Scientific methods

Structure analyses use the displayed frame. Coordinates and distances are in angstroms, angles are in degrees. Ordinary analysis uses nonperiodic coordinates: unwrap periodic molecules before analysis. The [trajectory tools](trajectories.md) also provide per-frame RMSD and an explicit minimum-image distance for orthogonal cells.

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

For native structures or snapshots, the result contains `contacts`, ordered `atom_ids` for both axes, and `cutoff`. Set `atom_name="CA"` for a C-alpha map. The GUI offers C-alpha or all-atom selection, an adjustable distance cutoff, and a colored contact matrix with its unique pair count. Hover over the image to inspect axis IDs.

```julia
contacts = contactmap(active_structure(); cutoff=8.0, atom_name="CA")
contacts.atom_ids
contacts.contacts
```

Dense maps use quadratic time and quadratic bit storage. GUI maps are limited to 1,024 selected atoms; the Julia API accepts up to 20,000. Choose a subset for larger structures. GUI analysis is synchronous and may briefly block interaction.

## Other geometry

Distances, angles, dihedrals, and radius of gyration share the native geometry kernels. Angles with coincident vertices and dihedrals with coincident or collinear bonds are rejected. Radius of gyration is mass-weighted about the center of mass, using common atomic weights with carbon mass as the fallback for an unknown element.

## Numerical validation

Julia regression tests recover a known rotation and translation, verify determinant +1, exercise reflected and coplanar inputs, reject degenerate and nonfinite inputs, match reordered identities, and validate explicit subsets and mapping errors. Contact tests cover symmetry, an excluded diagonal, exact cutoff inclusion, atom-name selection, and invalid cutoffs. Native tests check snapshot buffer validation. The embedded GUI integration compares loaded structures and creates the contact image using the same Julia methods.
