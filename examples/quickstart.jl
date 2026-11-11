using JlMol

sample_root = pkgdir(JlMol)
protein = loadstructure(joinpath(sample_root, "resources", "1CRN.pdb"))
setrepresentation!(protein, :cartoon)
colorby!(protein, :secondary_structure)
println("Crambin atoms: ", atom_count(protein))
println("Radius of gyration (Å): ", round(radius_of_gyration(protein); digits=3))

water = loadstructure(joinpath(sample_root, "test", "data", "water.xyz"))
attachtrajectory!(water, joinpath(sample_root, "test", "data", "water_analysis.xyz"))
println("Water trajectory RMSD (Å): ", round.(trajectory_rmsd(water; align=false); digits=3))
setframe!(water, 3)
setvisible!(water, false)
activate!(protein)
