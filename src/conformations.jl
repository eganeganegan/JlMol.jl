function _coordinate_pca(coordinates::AbstractMatrix{<:Real}, components::Integer)
    features, samples = size(coordinates)
    samples >= 2 || throw(ArgumentError("PCA needs at least two sampled frames"))
    1 <= components <= min(features, samples) || throw(ArgumentError("Invalid PCA component count"))
    centered = Matrix{Float64}(coordinates)
    all(isfinite, centered) || throw(ArgumentError("PCA coordinates must be finite"))
    average = vec(sum(centered; dims=2)) / samples
    centered .-= average
    factor = svd!(centered; full=false)
    indices = 1:Int(components)
    variances = abs2.(factor.S) ./ (samples - 1)
    total = sum(variances)
    return (
        scores=Diagonal(factor.S[indices]) * factor.Vt[indices, :],
        loadings=factor.U[:, indices],
        mean_coordinates=average,
        explained_variance=variances[indices],
        explained_fraction=total > 0 ? variances[indices] ./ total : zeros(Float64, components),
    )
end

"""
    trajectory_pca([structure]; reference=1, align=true, stride=1, atom_ids=nothing, components=2)

Perform Cartesian PCA on mean-centered, equally weighted atom coordinates from
sampled frames. Optionally align each sample to the reference using the same
atoms before centering. Returns scores (components × frames), loadings,
mean coordinates, explained variance (Å²), fractions of total variance,
and the corresponding one-based frame numbers and stable atom IDs.
Periodic images are not corrected. This does not seek the viewport.
"""
function trajectory_pca(structure::ActiveStructure=active_structure(); reference::Integer=1, align::Bool=true,
                        stride::Integer=1, atom_ids::Union{Nothing,AbstractVector{<:Integer}}=nothing,
                        components::Integer=2)
    frames = _trajectory_samples(structure, stride)
    length(frames) >= 2 || throw(ArgumentError("PCA needs at least two sampled frames"))
    _, ids = _coordinate_snapshot(structure)
    indices = atom_ids === nothing ? nothing : _trajectory_atom_indices(ids, atom_ids)
    selected_ids = indices === nothing ? ids : ids[indices]
    features = 3 * length(selected_ids)
    1 <= components <= min(features, length(frames)) || throw(ArgumentError("Invalid PCA component count"))
    reference_positions = align ? _trajectory_positions(structure, reference, indices) : nothing
    coordinates = Matrix{Float64}(undef, features, length(frames))
    for (column, frame) in enumerate(frames)
        positions = _trajectory_positions(structure, frame, indices)
        fitted = align ? alignstructures(positions, reference_positions).coordinates : positions
        coordinates[:, column] = vec(fitted)
    end
    result = _coordinate_pca(coordinates, components)
    return (; result..., frames=collect(frames), atom_ids=selected_ids)
end

"""
    cluster_conformations(scores; k=2, max_iterations=100)

Deterministic Euclidean k-means on the supplied coordinates (one sample per column).
Returns assignments, centers, representative column indices, and convergence
status. Clusters describe the supplied coordinates; a low-dimensional projection
can discard conformational differences.
"""
function cluster_conformations(scores::AbstractMatrix{<:Real}; k::Integer=2, max_iterations::Integer=100)
    points = Matrix{Float64}(scores)
    dimensions, samples = size(points)
    dimensions > 0 && all(isfinite, points) || throw(ArgumentError("Clustering scores must be finite and nonempty"))
    1 <= k <= samples || throw(ArgumentError("Cluster count must be between 1 and the number of samples"))
    max_iterations > 0 || throw(ArgumentError("Iteration limit must be positive"))
    centers = Matrix{Float64}(undef, dimensions, k)
    centers[:, 1] = view(points, :, 1)
    function squared_distance(point, center)
        value = sum(abs2(points[axis, point] - centers[axis, center]) for axis in 1:dimensions)
        isfinite(value) || throw(ArgumentError("Clustering distances exceed the numerical range"))
        return value
    end
    nearest = fill(Inf, samples)
    for center in 2:k
        for point in 1:samples
            nearest[point] = min(nearest[point], squared_distance(point, center - 1))
        end
        distance, seed = findmax(nearest)
        distance > 0 || throw(ArgumentError("More clusters requested than distinct sample positions"))
        centers[:, center] = view(points, :, seed)
    end

    assignments = zeros(Int, samples)
    previous = similar(assignments)
    counts = zeros(Int, k)
    converged = false
    for _ in 1:max_iterations
        copyto!(previous, assignments)
        fill!(counts, 0)
        for point in 1:samples
            best = 1
            distance = squared_distance(point, best)
            for center in 2:k
                candidate = squared_distance(point, center)
                if candidate < distance
                    best, distance = center, candidate
                end
            end
            assignments[point] = best
            nearest[point] = distance
            counts[best] += 1
        end
        for center in 1:k
            counts[center] == 0 || continue
            # Move the least well represented point from a group that can spare one.
            _, seed = findmax([counts[assignments[i]] > 1 ? nearest[i] : -Inf for i in 1:samples])
            counts[assignments[seed]] -= 1
            assignments[seed] = center
            counts[center] = 1
        end
        fill!(centers, 0.0)
        for point in 1:samples, axis in 1:dimensions
            centers[axis, assignments[point]] += points[axis, point] / counts[assignments[point]]
        end
        if assignments == previous
            converged = true
            break
        end
    end
    representatives = zeros(Int, k)
    distances = fill(Inf, k)
    for point in 1:samples
        center = assignments[point]
        distance = squared_distance(point, center)
        if distance < distances[center]
            representatives[center] = point
            distances[center] = distance
        end
    end
    return (; assignments, centers, representatives, converged)
end
