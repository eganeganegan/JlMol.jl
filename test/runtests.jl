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
    c, s = cos(0.37), sin(0.37)
    u, v = cos(0.49), sin(0.49)
    tilted = [c -s 0; s c 0; 0 0 1] * [1 0 0; 0 u -v; 0 v u]
    @test minimum_image_displacement(tilted * delta, Float64.(Float32.(tilted * cell))) ≈ tilted * expected atol=1e-6
    @test_throws ArgumentError minimum_image_displacement(delta, zeros(3, 3))
    @test_throws ArgumentError minimum_image_displacement(delta, [10.0 3.0 0.0; 0.0 8.0 0.0; 0.0 0.0 12.0])
    @test_throws ArgumentError minimum_image_displacement([NaN, 0.0, 0.0], cell)
end

@testset "Trajectory fluctuations and atom scope" begin
    first = [0.0 10; 0 0; 0 0]
    second = first + [2.0 0; 0 0; 0 0]
    third = first + [4.0 0; 0 0; 0 0]
    @test JlMol._rmsf((first, second, third)) ≈ [sqrt(8 / 3), 0]
    @test JlMol._rmsf((first,)) == [0, 0]
    @test JlMol._rmsf((first .+ 1e9, second .+ 1e9, third .+ 1e9)) ≈ [sqrt(8 / 3), 0]
    @test_throws ArgumentError JlMol._rmsf(())
    @test_throws ArgumentError JlMol._rmsf((first, zeros(3, 1)))
    @test JlMol._trajectory_atom_indices(UInt64[11, 12, 13], [13, 11]) == [3, 1]
    @test_throws ArgumentError JlMol._trajectory_atom_indices(UInt64[11, 12], [11, 11])
    @test_throws ArgumentError JlMol._trajectory_atom_indices(UInt64[11, 12], [13])
    @test_throws ArgumentError JlMol._trajectory_atom_indices(UInt64[11, 12], Int[])
end

@testset "Cartesian PCA and projected clusters" begin
    coordinates = [2.0 -2 0 0; 0 0 1 -1; 5 5 5 5]
    pca = JlMol._coordinate_pca(coordinates, 2)
    @test pca.explained_variance ≈ [8 / 3, 2 / 3]
    @test pca.explained_fraction ≈ [0.8, 0.2]
    @test pca.loadings * pca.scores .+ pca.mean_coordinates ≈ coordinates
    @test pca.loadings' * pca.loadings ≈ Matrix{Float64}(I, 2, 2)
    @test JlMol._coordinate_pca(coordinates .+ [100, -50, 7], 1).explained_fraction ≈ [0.8]
    @test JlMol._coordinate_pca(fill(3.0, 3, 4), 2).explained_fraction == [0, 0]
    @test_throws ArgumentError JlMol._coordinate_pca(ones(3, 1), 2)
    @test_throws ArgumentError JlMol._coordinate_pca(coordinates, 4)
    @test_throws ArgumentError JlMol._coordinate_pca(fill(Inf, 3, 4), 2)

    points = [0.0 0 10 10; 0 1 0 1]
    groups = cluster_conformations(points)
    @test groups.converged
    @test groups.assignments == [1, 1, 2, 2]
    @test groups.centers ≈ [0.0 10; 0.5 0.5]
    @test groups.representatives == [1, 3]
    @test !cluster_conformations(points; max_iterations=1).converged
    @test_throws ArgumentError cluster_conformations(points; k=5)
    @test_throws ArgumentError cluster_conformations(zeros(2, 4))
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

@testset "Local plugin lifecycle" begin
    mktempdir() do directory
        file = joinpath(directory, "Custom analysis λ.jl")
        write(file, """
            module ExamplePlugin
            using JlMol
            const PLUGIN = PluginMetadata(:test_example, "Example", v"2.3.0")
            function initialize_plugin!(registration)
                register_command!(registration, :answer, "Answer", () -> 42)
                register_command!(registration, :fail, "Fail", () -> error("test failure"))
                register_analysis!(registration, :summary, "Summary", atom_count)
                error("initialization failed after registration")
            end
            end
            """)
        @test_throws ErrorException loadplugin!(file)
        @test isempty(plugins())
        @test_throws ArgumentError run_plugin_command!(:test_example, :answer)

        source = read(file, String)
        write(file, replace(source, "error(\"initialization failed after registration\")" => "nothing"))
        metadata = loadplugin!(file)
        @test metadata.id == :test_example && metadata.version == v"2.3.0"
        @test only(plugins()).enabled
        @test run_plugin_command!(:test_example, :answer) == 42
        @test_throws ErrorException run_plugin_command!(:test_example, :fail)
        @test_throws ArgumentError run_plugin_command!(:test_example, :summary)
        @test_throws ArgumentError loadplugin!(file)

        duplicate = joinpath(directory, "duplicate.jl")
        cp(file, duplicate)
        @test_throws ArgumentError loadplugin!(duplicate)
        @test !enableplugin!(:test_example, false)
        @test_throws ArgumentError run_plugin_command!(:test_example, :answer)
        @test enableplugin!(:test_example, true)
        @test run_plugin_command!(:test_example, :answer) == 42

        incompatible = joinpath(directory, "new_api.jl")
        write(incompatible, replace(source, ":test_example" => ":test_incompatible",
                                    "v\"2.3.0\")" => "v\"2.3.0\"; api_version=v\"99\")"))
        @test_throws ArgumentError loadplugin!(incompatible)
        @test length(plugins()) == 1
    end
end

