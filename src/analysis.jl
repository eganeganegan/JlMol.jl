"An owned coordinate/identity copy of one native structure. Coordinates are 3×N in angstroms."
struct StructureSnapshot
    coordinates::Matrix{Float64}
    atom_ids::Vector{UInt64}
    atom_keys::Vector{String}
    atom_names::Vector{String}
end

"Copy coordinates and atom identities without borrowing native memory."
function snapshot(structure::ActiveStructure = active_structure())
    count = atom_count(structure)
    coordinates = Matrix{Float64}(undef, 3, count)
    ids = Vector{UInt64}(undef, count)
    _check(ccall(_symbol(:jlmol_copy_object_atoms), Cint,
                 (UInt64, Ptr{Cdouble}, Ptr{UInt64}, Csize_t),
                 structure.id, coordinates, ids, count))
    function field(index, key)
        value = unsafe_string(ccall(_symbol(:jlmol_object_atom_field), Cstring,
                                    (UInt64, Csize_t, Cstring), structure.id, index - 1, key))
        isempty(value) && error(_last_error("Could not copy atom metadata"))
        return value
    end
    return StructureSnapshot(coordinates, ids, [field(i, "identity") for i in 1:count],
                             [field(i, "name") for i in 1:count])
end

"A proper rigid transform, its aligned coordinate copy, and the atom correspondence used."
struct AlignmentResult
    rotation::Matrix{Float64}
    translation::Vector{Float64}
    coordinates::Matrix{Float64}
    rmsd::Float64
    mobile_ids::Vector{UInt64}
    reference_ids::Vector{UInt64}
end

function _coordinates(values::AbstractMatrix{<:Real})
    size(values, 1) == 3 || throw(ArgumentError("Coordinates must be a 3×N matrix"))
    size(values, 2) > 0 || throw(ArgumentError("At least one atom is required"))
    result = Matrix{Float64}(values)
    all(isfinite, result) || throw(ArgumentError("Coordinates must be finite"))
    return result
end

function _paired_coordinates(mobile, reference)
    first, second = _coordinates(mobile), _coordinates(reference)
    size(first) == size(second) || throw(ArgumentError("Atom correspondence must have equal sizes"))
    return first, second
end

"""
    alignstructures(mobile, reference)

Fit corresponding columns of two 3×N matrices with a Kabsch proper rotation.
At least three non-collinear points are required. Returns a copy; inputs are unchanged.
No periodic-boundary correction is applied.
"""
function alignstructures(mobile::AbstractMatrix{<:Real}, reference::AbstractMatrix{<:Real})
    first, second = _paired_coordinates(mobile, reference)
    count = size(first, 2)
    count >= 3 || throw(ArgumentError("Rigid alignment requires at least three atoms"))
    first_center = sum(first; dims=2) / count
    second_center = sum(second; dims=2) / count
    x, y = first .- first_center, second .- second_center
    rank(x) >= 2 && rank(y) >= 2 ||
        throw(ArgumentError("Rigid alignment requires non-collinear atom positions"))
    fit = svd(x * transpose(y))
    correction = Diagonal([1.0, 1.0, det(fit.V * transpose(fit.U)) < 0 ? -1.0 : 1.0])
    rotation = fit.V * correction * transpose(fit.U)
    translation = vec(second_center - rotation * first_center)
    aligned = rotation * first .+ translation
    value = sqrt(sum(abs2, aligned - second) / count)
    return AlignmentResult(rotation, translation, aligned, value, UInt64[], UInt64[])
end

"RMSD in angstroms; corresponding matrix columns are aligned by default."
function rmsd(mobile::AbstractMatrix{<:Real}, reference::AbstractMatrix{<:Real}; align::Bool=true)
    align && return alignstructures(mobile, reference).rmsd
    first, second = _paired_coordinates(mobile, reference)
    return sqrt(sum(abs2, first - second) / size(first, 2))
end

function _validate_snapshot(value::StructureSnapshot)
    count = size(value.coordinates, 2)
    count == length(value.atom_ids) == length(value.atom_keys) == length(value.atom_names) ||
        throw(ArgumentError("Snapshot coordinates and metadata have different sizes"))
    length(unique(value.atom_ids)) == count || throw(ArgumentError("Atom IDs must be unique"))
    return value
end

function _correspondence(mobile::StructureSnapshot, reference::StructureSnapshot, atom_pairs)
    _validate_snapshot(mobile)
    _validate_snapshot(reference)
    if atom_pairs === nothing
        length(unique(mobile.atom_keys)) == length(mobile.atom_keys) &&
            length(unique(reference.atom_keys)) == length(reference.atom_keys) ||
            throw(ArgumentError("Ambiguous atom identities; provide explicit atom_pairs"))
        lookup = Dict(key => i for (i, key) in enumerate(reference.atom_keys))
        length(mobile.atom_keys) == length(reference.atom_keys) &&
            all(key -> haskey(lookup, key), mobile.atom_keys) ||
            throw(ArgumentError("Atom identities differ; provide explicit atom_pairs for a matched subset"))
        first = collect(eachindex(mobile.atom_ids))
        second = [lookup[key] for key in mobile.atom_keys]
    else
        pairs = collect(atom_pairs)
        isempty(pairs) && throw(ArgumentError("Atom mapping cannot be empty"))
        mobile_lookup = Dict(id => i for (i, id) in enumerate(mobile.atom_ids))
        reference_lookup = Dict(id => i for (i, id) in enumerate(reference.atom_ids))
        first, second = Int[], Int[]
        for pair in pairs
            length(pair) == 2 || throw(ArgumentError("Each mapping entry must contain two atom IDs"))
            a, b = UInt64(pair[1]), UInt64(pair[2])
            haskey(mobile_lookup, a) && haskey(reference_lookup, b) ||
                throw(ArgumentError("Mapped atom ID does not exist in its structure"))
            push!(first, mobile_lookup[a])
            push!(second, reference_lookup[b])
        end
        length(unique(first)) == length(first) && length(unique(second)) == length(second) ||
            throw(ArgumentError("Atom correspondence must be one-to-one"))
    end
    return first, second
end

"""
    alignstructures(mobile, reference; atom_pairs=nothing)

Match complete structures by model, chain, residue number/name, atom name, and alternate
location, or supply one-to-one `(mobile_id, reference_id)` tuples for a matched subset.
Returns the transform, fitted coordinates, RMSD, and ordered IDs. No scene mutation occurs.
"""
function alignstructures(mobile::StructureSnapshot, reference::StructureSnapshot; atom_pairs=nothing)
    first, second = _correspondence(mobile, reference, atom_pairs)
    fit = alignstructures(mobile.coordinates[:, first], reference.coordinates[:, second])
    return AlignmentResult(fit.rotation, fit.translation, fit.coordinates, fit.rmsd,
                           mobile.atom_ids[first], reference.atom_ids[second])
end

alignstructures(mobile::ActiveStructure, reference::ActiveStructure; kwargs...) =
    alignstructures(snapshot(mobile), snapshot(reference); kwargs...)

function rmsd(mobile::StructureSnapshot, reference::StructureSnapshot; align::Bool=true, atom_pairs=nothing)
    first, second = _correspondence(mobile, reference, atom_pairs)
    return rmsd(mobile.coordinates[:, first], reference.coordinates[:, second]; align)
end

rmsd(mobile::ActiveStructure, reference::ActiveStructure; kwargs...) =
    rmsd(snapshot(mobile), snapshot(reference); kwargs...)

"""
    contactmap(coordinates; cutoff=8.0)

Return a symmetric atom contact BitMatrix for a 3×N coordinate matrix. A pair is in
contact when its nonperiodic Euclidean distance is at most cutoff angstroms.
The diagonal is false. No bonds or sequence neighbors are excluded.
"""
function contactmap(coordinates::AbstractMatrix{<:Real}; cutoff::Real=8.0)
    isfinite(cutoff) && cutoff > 0 || throw(ArgumentError("Contact cutoff must be finite and positive"))
    points = _coordinates(coordinates)
    count = size(points, 2)
    count <= 20_000 || throw(ArgumentError("Dense contact maps are limited to 20,000 atoms; select a subset"))
    contacts = falses(count, count)
    squared_cutoff = Float64(cutoff)^2
    for second in 2:count, first in 1:(second - 1)
        squared_distance = 0.0
        for axis in 1:3
            squared_distance += (points[axis, first] - points[axis, second])^2
        end
        if squared_distance <= squared_cutoff
            contacts[first, second] = contacts[second, first] = true
        end
    end
    return contacts
end

"Return contacts with their ordered atom IDs, optionally selecting atom names such as CA."
function contactmap(value::StructureSnapshot; cutoff::Real=8.0, atom_name::Union{Nothing,AbstractString}=nothing)
    _validate_snapshot(value)
    indices = atom_name === nothing ? collect(eachindex(value.atom_ids)) :
              findall(==(atom_name), value.atom_names)
    isempty(indices) && throw(ArgumentError("No atoms match the contact map selection"))
    return (contacts=contactmap(value.coordinates[:, indices]; cutoff),
            atom_ids=value.atom_ids[indices], cutoff=Float64(cutoff))
end

contactmap(structure::ActiveStructure = active_structure(); kwargs...) = contactmap(snapshot(structure); kwargs...)
