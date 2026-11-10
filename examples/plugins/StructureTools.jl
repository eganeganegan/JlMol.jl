module StructureTools

using JlMol

const PLUGIN = PluginMetadata(:structure_tools, "Structure Tools", v"1.0.0";
                              description="Show space filling, summarize a structure, and color by distance from its center.")

function show_space_filling()
    setrepresentation!(:spacefill)
    return "Showing space-filling atoms"
end

function summarize(structure)
    radius = round(radius_of_gyration(structure); digits=3)
    return "$(atom_count(structure)) atoms\n$(bond_count(structure)) bonds\nRadius of gyration: $radius Å"
end

function radial_colors(structure)
    coordinates = snapshot(structure).coordinates
    center = sum(coordinates; dims=2) / size(coordinates, 2)
    distances = sqrt.(vec(sum(abs2, coordinates .- center; dims=1)))
    fraction = distances ./ max(maximum(distances), eps(Float64))
    # Blue at the center, orange at the outside.
    return permutedims(hcat(0.15 .+ 0.85 .* fraction, 0.45 .- 0.12 .* fraction, 0.95 .- 0.80 .* fraction))
end

function initialize_plugin!(registration)
    register_command!(registration, :space_filling, "Show Space Filling", show_space_filling)
    register_analysis!(registration, :summary, "Structure Summary", summarize)
    register_coloring!(registration, :radial, "Distance from Center", radial_colors)
end

end
