using Test
using JlMol

@testset "JlMol Julia bridge" begin
    @test JlMol.active_structure() isa JlMol.ActiveStructure
    # The standalone test process has no active GUI scene, but validates that the
    # package resolves the built C ABI and returns safe empty-state values.
    @test JlMol.atom_count() == 0
    @test JlMol.bond_count() == 0
    @test JlMol.selected_atom() === nothing
end

