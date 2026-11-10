"""
    attachtrajectory!(structure, path)

Attach a trajectory to the structure's existing topology and display its first frame.
Atom count and ordering must match the topology. Coordinates are in angstroms.
"""
function attachtrajectory!(structure::ActiveStructure, path::AbstractString)
    _check(ccall(_symbol(:jlmol_attach_trajectory), Cint, (UInt64, Cstring), structure.id, path))
    return structure
end

attachtrajectory!(path::AbstractString) = attachtrajectory!(active_structure(), path)

"""
    trajectory_info([structure])

Return the frame count, one-based displayed frame, time in picoseconds when known,
and the periodic cell as a matrix of column vectors in angstroms. Return `nothing`
for a valid structure without a trajectory.
"""
function trajectory_info(structure::ActiveStructure=active_structure())
    count = Ref{Csize_t}()
    index = Ref{Csize_t}()
    time = Ref{Cdouble}()
    cell = Matrix{Float64}(undef, 3, 3)
    _check(ccall(_symbol(:jlmol_trajectory_state), Cint,
        (UInt64, Ref{Csize_t}, Ref{Csize_t}, Ref{Cdouble}, Ptr{Cdouble}),
        structure.id, count, index, time, cell))
    iszero(count[]) && return nothing
    return (frames=Int(count[]), frame=Int(index[]) + 1, time_ps=isnan(time[]) ? nothing : time[],
            cell=all(iszero, cell) ? nothing : cell)
end

function _frame_index(frame::Integer)
    frame >= 1 || throw(ArgumentError("Trajectory frame indices start at 1"))
    return Csize_t(frame - 1)
end

"Display the given one-based frame without changing structure identity or styling."
function setframe!(structure::ActiveStructure, frame::Integer)
    _check(ccall(_symbol(:jlmol_set_trajectory_frame), Cint, (UInt64, Csize_t),
        structure.id, _frame_index(frame)))
    return frame
end

setframe!(frame::Integer) = setframe!(active_structure(), frame)

"Read owned 3 × N frame coordinates without changing the displayed frame."
function trajectory_frame(structure::ActiveStructure, frame::Integer)
    count = atom_count(structure)
    coordinates = Matrix{Float64}(undef, 3, count)
    _check(ccall(_symbol(:jlmol_copy_trajectory_frame), Cint, (UInt64, Csize_t, Ptr{Cdouble}, Csize_t),
        structure.id, _frame_index(frame), coordinates, count))
    return coordinates
end

trajectory_frame(frame::Integer) = trajectory_frame(active_structure(), frame)

"""
    trajectory_rmsd([structure]; reference=1, align=true)

Calculate RMSD in angstroms against a one-based reference frame, using the attached
topology's atom order. This streams frames without moving the viewport. Coordinates
are used as stored, with no periodic correction.
"""
function trajectory_rmsd(structure::ActiveStructure=active_structure(); reference::Integer=1, align::Bool=true)
    info = trajectory_info(structure)
    info === nothing && throw(ArgumentError("No trajectory is attached to this structure"))
    reference_positions = trajectory_frame(structure, reference)
    return [rmsd(trajectory_frame(structure, frame), reference_positions; align) for frame in 1:info.frames]
end

"""
    minimum_image_displacement(displacement, cell)

Return the shortest image of a 3D displacement for a periodic cell with orthogonal
basis vectors (columns, in angstroms). Rotated orthogonal cells are supported;
skewed cells are rejected. Neither argument is changed.
"""
function minimum_image_displacement(displacement::AbstractVector{<:Real}, cell::AbstractMatrix{<:Real})
    length(displacement) == 3 || throw(ArgumentError("The displacement must have three components"))
    size(cell) == (3, 3) || throw(ArgumentError("The cell must have three basis vectors"))
    delta = Vector{Float64}(displacement)
    basis = Matrix{Float64}(cell)
    all(isfinite, delta) && all(isfinite, basis) || throw(ArgumentError("Displacement and cell must be finite"))
    lengths = [norm(view(basis, :, axis)) for axis in 1:3]
    all(x -> isfinite(x) && x > 0, lengths) || throw(ArgumentError("Cell vectors must have positive finite lengths"))
    directions = basis ./ reshape(lengths, 1, 3)
    all(abs(dot(view(directions, :, a), view(directions, :, b))) <= 1e-10
        for (a, b) in ((1, 2), (1, 3), (2, 3))) ||
        throw(ArgumentError("Minimum-image distance currently requires an orthogonal cell"))
    fractional = transpose(directions) * delta ./ lengths
    all(isfinite, fractional) || throw(ArgumentError("Displacement exceeds the cell's numerical range"))
    return basis * (fractional .- round.(fractional))
end

"Measure two atom IDs in the displayed frame using its orthogonal periodic cell."
function periodic_distance(structure::ActiveStructure, first_atom_id::Integer, second_atom_id::Integer)
    info = trajectory_info(structure)
    (info === nothing || info.cell === nothing) && throw(ArgumentError("The displayed frame has no periodic cell"))
    data = snapshot(structure)
    first = findfirst(==(first_atom_id), data.atom_ids)
    second = findfirst(==(second_atom_id), data.atom_ids)
    (first === nothing || second === nothing) && throw(ArgumentError("An atom ID does not exist in this structure"))
    delta = data.coordinates[:, second] - data.coordinates[:, first]
    return norm(minimum_image_displacement(delta, info.cell))
end

periodic_distance(first_atom_id::Integer, second_atom_id::Integer) =
    periodic_distance(active_structure(), first_atom_id, second_atom_id)
