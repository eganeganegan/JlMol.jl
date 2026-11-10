using Test
using JlMol
using LinearAlgebra

@testset "JlMol Julia bridge" begin
    @test JlMol.active_structure() isa JlMol.ActiveStructure
    @test isempty(JlMol.structures())
    # The standalone test process has no active GUI scene, but validates that the
    # package resolves the built C ABI and returns safe empty-state values.
    @test JlMol.atom_count() == 0
    @test JlMol.bond_count() == 0
    @test JlMol.selected_atom() === nothing
    @test_throws ErrorException snapshot()
    @test_throws ErrorException opacity()
    @test_throws ErrorException setopacity!(0.5)
    @test_throws ErrorException trajectory_info()
    @test_throws ArgumentError setframe!(0)
end

@testset "Orthogonal periodic cells" begin
    cell = Matrix(Diagonal([10.0, 8.0, 12.0]))
    delta = [9.5, -7.5, 6.1]
    expected = [-0.5, 0.5, -5.9]
    @test minimum_image_displacement(delta, cell) ≈ expected
    @test minimum_image_displacement(delta + cell * [3, -2, 2], cell) ≈ expected
    rotation = [0.0 -1.0 0.0; 1.0 0.0 0.0; 0.0 0.0 1.0]
    @test minimum_image_displacement(rotation * delta, rotation * cell) ≈ rotation * expected
    @test_throws ArgumentError minimum_image_displacement(delta, zeros(3, 3))
    @test_throws ArgumentError minimum_image_displacement(delta, [10.0 3.0 0.0; 0.0 8.0 0.0; 0.0 0.0 12.0])
    @test_throws ArgumentError minimum_image_displacement([NaN, 0.0, 0.0], cell)
end

@testset "Rigid alignment and RMSD" begin
    points = [0.0 2 0 0 1; 0 0 3 0 1; 0 0 0 4 1]
    rotation = [0.0 -1 0; 1 0 0; 0 0 1]
    shift = [5.0, -3, 2]
    reference = rotation * points .+ shift
    original = copy(points)
    fit = alignstructures(points, reference)
    @test fit.rotation ≈ rotation atol=1e-12
    @test fit.translation ≈ shift atol=1e-12
    @test fit.coordinates ≈ reference atol=1e-12
    @test fit.rmsd < 1e-12
    @test det(fit.rotation) ≈ 1 atol=1e-12
    @test points == original
    @test rmsd(points, reference) < 1e-12
    @test rmsd(points, points .+ [1, 2, 2]; align=false) ≈ 3
    @test alignstructures(points[:, 1:3], reference[:, 1:3]).rmsd < 1e-12

    reflected = Diagonal([-1.0, 1, 1]) * points
    mirror_fit = alignstructures(points, reflected)
    @test det(mirror_fit.rotation) ≈ 1 atol=1e-12
    @test mirror_fit.rmsd > 0.1
    @test_throws ArgumentError alignstructures(points[:, 1:2], reference[:, 1:2])
    @test_throws ArgumentError alignstructures(zeros(3, 3), zeros(3, 3))
    @test_throws ArgumentError alignstructures([0.0 1 2; 0 0 0; 0 0 0], zeros(3, 3))
    @test_throws ArgumentError rmsd(points, reference[:, 1:4])
    @test_throws ArgumentError rmsd(zeros(2, 5), points)
    @test_throws ArgumentError rmsd(zeros(3, 0), zeros(3, 0); align=false)
    nonfinite = copy(points)
    nonfinite[1, 1] = NaN
    @test_throws ArgumentError rmsd(nonfinite, reference)
end

@testset "Identity matching and explicit correspondence" begin
    points = [0.0 2 0 0; 0 0 3 0; 0 0 0 4]
    ids = UInt64[10, 20, 30, 40]
    keys = ["N", "CA", "C", "O"]
    names = copy(keys)
    mobile = StructureSnapshot(points, ids, keys, names)
    order = [4, 2, 1, 3]
    reference = StructureSnapshot(points[:, order] .+ [3, -1, 2], ids[order] .+ 100,
                                  keys[order], names[order])
    fit = alignstructures(mobile, reference)
    @test fit.rmsd < 1e-12
    @test fit.mobile_ids == ids
    @test fit.reference_ids == ids .+ 100
    @test rmsd(mobile, reference; align=false) ≈ sqrt(14)
    pairs = [(10, 110), (20, 120), (30, 130)]
    subset = alignstructures(mobile, reference; atom_pairs=pairs)
    @test subset.rmsd < 1e-12
    @test subset.mobile_ids == [10, 20, 30]
    @test subset.reference_ids == [110, 120, 130]
    @test_throws ArgumentError alignstructures(mobile, reference; atom_pairs=[(10, 110), (10, 120)])
    @test_throws ArgumentError alignstructures(mobile, reference; atom_pairs=[(10, 999)])
    @test_throws ArgumentError alignstructures(mobile, reference; atom_pairs=[])
    ambiguous = StructureSnapshot(points, ids, ["N", "N", "C", "O"], names)
    @test_throws ArgumentError rmsd(ambiguous, reference)
    @test rmsd(ambiguous, reference; atom_pairs=pairs) < 1e-12
    different = StructureSnapshot(points, ids, ["N", "CB", "C", "O"], names)
    @test_throws ArgumentError rmsd(mobile, different)
end

@testset "Nonperiodic contact maps" begin
    points = [0.0 1 2.01 10; 0 0 0 0; 0 0 0 0]
    contacts = contactmap(points; cutoff=1)
    @test size(contacts) == (4, 4)
    @test contacts == transpose(contacts)
    @test !any(diag(contacts))
    @test contacts[1, 2]
    @test !contacts[2, 3]
    @test sum(contacts) == 2
    @test contactmap(points; cutoff=1.01)[2, 3]
    @test sum(contactmap(points[:, 1:1])) == 0
    @test_throws ArgumentError contactmap(points; cutoff=0)
    @test_throws ArgumentError contactmap(points; cutoff=Inf)
    @test_throws ArgumentError contactmap(points; cutoff=NaN)
    data = StructureSnapshot(points, UInt64[1, 2, 3, 4], ["a", "b", "c", "d"], ["CA", "N", "CA", "O"])
    selected = contactmap(data; atom_name="CA", cutoff=3)
    @test selected.atom_ids == [1, 3]
    @test selected.contacts == Bool[0 1; 1 0]
    @test_throws ArgumentError contactmap(data; atom_name="P")
end

