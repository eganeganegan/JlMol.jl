using JlMol
using LinearAlgebra
using Test
using TOML

const reference_dir = joinpath(@__DIR__, "data")
const crambin_pdb = joinpath(@__DIR__, "..", "resources", "1CRN.pdb")
const crambin_trr = joinpath(reference_dir, "crambin_reference.trr")
const reference_data = TOML.parsefile(joinpath(reference_dir, "crambin_reference.toml"))
const length_atol = 1e-5             # Å, PDB/trajectory coordinates pass through Float32 storage.
const angle_atol = 2e-4              # Degrees, sensitivity to rounding of short bonds.
const rotation_atol = 2e-7           # Dimensionless matrix elements.

within(actual::Number, expected::Number, atol) = isapprox(actual, expected; atol, rtol=0)
within(actual, expected, atol) = size(actual) == size(expected) && all(isapprox.(actual, expected; atol, rtol=0))

@testset "Crambin scientific reference" begin
    mol = loadstructure(crambin_pdb)
    try
        structure = reference_data["structure"]
        trajectory = reference_data["trajectory"]
        original = snapshot(mol)

        @testset "Biopython geometry and SciPy contacts" begin
            @test original.atom_ids == UInt64.(1:327)
            @test within(distance(mol, structure["distance_ids"]...), structure["distance"], length_atol)
            for (ids, expected) in zip(structure["angle_ids"], structure["angles"])
                @test within(angle(mol, ids...), expected, angle_atol)
            end
            for (ids, expected) in zip(structure["dihedral_ids"], structure["dihedrals"])
                @test within(dihedral(mol, ids...), expected, angle_atol)
            end
            @test_throws ErrorException angle(mol, 1, 1, 2)
            @test_throws ErrorException dihedral(mol, 1, 2, 2, 3)

            map = contactmap(mol; cutoff=structure["contact_cutoff"], atom_name="CA")
            @test map.atom_ids == structure["contact_ids"]
            @test map.contacts == transpose(map.contacts) && !any(diag(map.contacts))
            observed = Set((map.atom_ids[i], map.atom_ids[j]) for j in axes(map.contacts, 2)
                           for i in 1:j-1 if map.contacts[i, j])
            @test observed == Set(Tuple(pair) for pair in structure["contact_pairs"])
        end

        attachtrajectory!(mol, crambin_trr)
        sample = trajectory["sample_ids"]
        subset = trajectory["subset_ids"]
        setframe!(mol, 4)
        saved_state = trajectory_info(mol)
        displayed = snapshot(mol)

        @testset "Independent TRR records and periodic distances" begin
            @test saved_state.frames == 5
            @test displayed.atom_ids == original.atom_ids
            @test within(displayed.coordinates[:, sample], stack(trajectory["sample_positions"][4]), length_atol)
            for frame in (5, 1, 3, 2, 4)
                coordinates = trajectory_frame(mol, frame)
                @test size(coordinates) == (3, 327)
                @test within(coordinates[:, sample], stack(trajectory["sample_positions"][frame]), length_atol)
                @test trajectory_info(mol) == saved_state
                setframe!(mol, frame)
                info = trajectory_info(mol)
                @test info.frame == frame
                @test info.time_ps == trajectory["times_ps"][frame]
                @test within(info.cell, Diagonal(trajectory["box_lengths"][frame]), length_atol)
                setframe!(mol, saved_state.frame)
            end
            @test within(distance(mol, trajectory["periodic_ids"]...), trajectory["nonperiodic_distance"], length_atol)
            @test within(periodic_distance(mol, trajectory["periodic_ids"]...), trajectory["periodic_distance"], length_atol)
            @test_throws ErrorException setframe!(mol, 6)
            @test trajectory_info(mol) == saved_state
        end

        @testset "SciPy rigid transform and trajectory statistics" begin
            expected = reference_data["fit_3_to_2"]
            target = loadstructure(crambin_pdb)
            fitted = try
                attachtrajectory!(target, crambin_trr)
                setframe!(target, 2)
                setframe!(mol, 3)
                fit = alignstructures(mol, target)
                @test fit.mobile_ids == fit.reference_ids == original.atom_ids
                @test within(rmsd(mol, target; align=false), trajectory["rmsd_raw"][3], length_atol)
                @test trajectory_info(mol).frame == 3 && trajectory_info(target).frame == 2
                fit
            finally
                close!(target)
                setframe!(mol, saved_state.frame)
            end
            @test within(det(fitted.rotation), 1.0, 1e-12)
            @test within(fitted.rotation, transpose(stack(expected["rotation"])), rotation_atol)
            @test within(fitted.translation, expected["translation"], length_atol)
            @test within(fitted.coordinates[:, sample], stack(expected["positions"]), length_atol)
            @test within(fitted.rmsd, trajectory["rmsd_aligned"][3], length_atol)

            for align in (false, true)
                suffix = align ? "aligned" : "raw"
                @test within(trajectory_rmsd(mol; reference=2, align), trajectory["rmsd_$suffix"], length_atol)
                fluctuation = trajectory_rmsf(mol; reference=2, align)
                @test fluctuation.atom_ids == original.atom_ids
                @test within(fluctuation.values[sample], trajectory["rmsf_$suffix"], length_atol)
                @test within(trajectory_rmsd(mol; reference=2, align, stride=2, atom_ids=subset),
                             trajectory["subset_rmsd_$suffix"], length_atol)
                selected = trajectory_rmsf(mol; reference=2, align, stride=2, atom_ids=subset)
                @test selected.atom_ids == subset
                @test within(selected.values, trajectory["subset_rmsf_$suffix"], length_atol)
            end
            @test trajectory_info(mol) == saved_state
            @test snapshot(mol).coordinates == displayed.coordinates
        end

        @testset "Coordinate-free TRR frames" begin
            attachtrajectory!(mol, joinpath(reference_dir, "crambin_missing_positions.trr"))
            before = trajectory_info(mol)
            coordinates = snapshot(mol).coordinates
            @test before.frames == 2 && before.frame == 1
            @test_throws ErrorException trajectory_frame(mol, 2)
            @test_throws ErrorException setframe!(mol, 2)
            @test_throws ErrorException trajectory_rmsd(mol; align=false)
            @test_throws ErrorException trajectory_rmsf(mol; align=false)
            @test trajectory_info(mol) == before
            @test snapshot(mol).coordinates == coordinates
        end
    finally
        close!(mol)
    end
end
