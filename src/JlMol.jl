module JlMol

import Libdl
using LinearAlgebra
import Base: angle

export ActiveStructure, active_structure, structures, loadstructure, activate!, close!,
       name, atom_count, bond_count, isvisible, setvisible!, opacity, setopacity!, representation,
       setrepresentation!, color_scheme, colorby!, selected_atom, distance, angle,
       dihedral, radius_of_gyration, StructureSnapshot, snapshot, AlignmentResult,
       alignstructures, rmsd, contactmap, attachtrajectory!, trajectory_info, setframe!,
       trajectory_frame, trajectory_rmsd, trajectory_rmsf, trajectory_pca, cluster_conformations,
       minimum_image_displacement, periodic_distance

"A stable handle to an application-owned molecular structure."
struct ActiveStructure
    id::UInt64
end

const _library_handle = Ref{Ptr{Cvoid}}(C_NULL)

function _library()
    path = get(ENV, "JLMOL_CORE_LIBRARY", "")
    isempty(path) && error("JLMOL_CORE_LIBRARY is not set; launch this package inside JlMol or point it at libjlmol_core")
    return path
end

function _symbol(name::Symbol)
    if _library_handle[] == C_NULL
        _library_handle[] = Libdl.dlopen(_library())
    end
    return Libdl.dlsym(_library_handle[], name)
end

function _last_error(fallback::AbstractString)
    message = unsafe_string(ccall(_symbol(:jlmol_get_last_error), Cstring, ()))
    return isempty(message) ? String(fallback) : message
end

function _check(code::Integer)
    code == 0 && return nothing
    error(_last_error("JlMol native operation failed with code $code"))
end

active_structure() = ActiveStructure(UInt64(ccall(_symbol(:jlmol_active_object_id), UInt64, ())))

function structures()
    count = Int(ccall(_symbol(:jlmol_object_count), Csize_t, ()))
    count == 0 && return ActiveStructure[]
    return [ActiveStructure(UInt64(ccall(_symbol(:jlmol_object_id_at), UInt64, (Csize_t,), index)))
            for index in 0:(count - 1)]
end

function loadstructure(path::AbstractString)
    id = UInt64(ccall(_symbol(:jlmol_load_structure), UInt64, (Cstring,), path))
    id == 0 && error(_last_error("Could not load structure"))
    return ActiveStructure(id)
end

function activate!(structure::ActiveStructure)
    _check(ccall(_symbol(:jlmol_set_active_object), Cint, (UInt64,), structure.id))
    return structure
end

function close!(structure::ActiveStructure)
    _check(ccall(_symbol(:jlmol_close_object), Cint, (UInt64,), structure.id))
    return nothing
end

name(structure::ActiveStructure = active_structure()) =
    unsafe_string(ccall(_symbol(:jlmol_object_name), Cstring, (UInt64,), structure.id))

atom_count(structure::ActiveStructure = active_structure()) =
    Int(ccall(_symbol(:jlmol_object_atom_count), Csize_t, (UInt64,), structure.id))

bond_count(structure::ActiveStructure = active_structure()) =
    Int(ccall(_symbol(:jlmol_object_bond_count), Csize_t, (UInt64,), structure.id))

isvisible(structure::ActiveStructure = active_structure()) =
    ccall(_symbol(:jlmol_object_visible), Cint, (UInt64,), structure.id) != 0

function setvisible!(structure::ActiveStructure, visible::Bool)
    _check(ccall(_symbol(:jlmol_set_object_visible), Cint, (UInt64, Cint), structure.id, visible))
    return visible
end

"Get the structure's opacity in [0, 1]. Visibility is independent."
function opacity(structure::ActiveStructure = active_structure())
    result = Ref{Cdouble}()
    _check(ccall(_symbol(:jlmol_object_opacity), Cint, (UInt64, Ref{Cdouble}), structure.id, result))
    return result[]
end

"Set opacity in [0, 1]: 0 hides geometry and picking; 1 is fully opaque."
function setopacity!(structure::ActiveStructure, value::Real)
    alpha = Float64(value)
    _check(ccall(_symbol(:jlmol_set_object_opacity), Cint, (UInt64, Cdouble), structure.id, alpha))
    return alpha
end

setopacity!(value::Real) = setopacity!(active_structure(), value)

representation(structure::ActiveStructure = active_structure()) =
    Symbol(unsafe_string(ccall(_symbol(:jlmol_object_representation), Cstring, (UInt64,), structure.id)))

color_scheme(structure::ActiveStructure = active_structure()) =
    Symbol(unsafe_string(ccall(_symbol(:jlmol_object_color_scheme), Cstring, (UInt64,), structure.id)))

function setrepresentation!(structure::ActiveStructure, value::Symbol)
    _check(ccall(_symbol(:jlmol_set_object_representation), Cint, (UInt64, Cstring), structure.id, string(value)))
    return value
end

setrepresentation!(value::Symbol) = setrepresentation!(active_structure(), value)

function colorby!(structure::ActiveStructure, value::Symbol)
    _check(ccall(_symbol(:jlmol_set_object_color_scheme), Cint, (UInt64, Cstring), structure.id, string(value)))
    return value
end

colorby!(value::Symbol) = colorby!(active_structure(), value)

function selected_atom()
    ccall(_symbol(:jlmol_has_selection), Cint, ()) == 0 && return nothing
    return (
        object_id = UInt64(ccall(_symbol(:jlmol_selected_object_id), UInt64, ())),
        id = UInt64(ccall(_symbol(:jlmol_selected_atom_id), UInt64, ())),
        serial = Int(ccall(_symbol(:jlmol_selected_atom_serial), Cint, ())),
        name = unsafe_string(ccall(_symbol(:jlmol_selected_atom_name), Cstring, ())),
        element = unsafe_string(ccall(_symbol(:jlmol_selected_atom_element), Cstring, ())),
        residue_name = unsafe_string(ccall(_symbol(:jlmol_selected_atom_residue_name), Cstring, ())),
        residue_id = Int(ccall(_symbol(:jlmol_selected_atom_residue_id), Cint, ())),
        chain = unsafe_string(ccall(_symbol(:jlmol_selected_atom_chain), Cstring, ())),
    )
end

function distance(structure::ActiveStructure, first_atom_id::Integer, second_atom_id::Integer)
    result = Ref{Cdouble}()
    code = ccall(_symbol(:jlmol_measure_distance), Cint,
                 (UInt64, UInt64, UInt64, Ref{Cdouble}), structure.id,
                 UInt64(first_atom_id), UInt64(second_atom_id), result)
    _check(code)
    return Float64(result[])
end
distance(first_atom_id::Integer, second_atom_id::Integer) =
    distance(active_structure(), first_atom_id, second_atom_id)

function angle(structure::ActiveStructure, first_atom_id::Integer, vertex_atom_id::Integer,
               third_atom_id::Integer)
    result = Ref{Cdouble}()
    code = ccall(_symbol(:jlmol_measure_angle), Cint,
                 (UInt64, UInt64, UInt64, UInt64, Ref{Cdouble}), structure.id,
                 UInt64(first_atom_id), UInt64(vertex_atom_id), UInt64(third_atom_id), result)
    _check(code)
    return Float64(result[])
end
angle(first_atom_id::Integer, vertex_atom_id::Integer, third_atom_id::Integer) =
    angle(active_structure(), first_atom_id, vertex_atom_id, third_atom_id)

function dihedral(structure::ActiveStructure, first_atom_id::Integer, second_atom_id::Integer,
                  third_atom_id::Integer, fourth_atom_id::Integer)
    result = Ref{Cdouble}()
    code = ccall(_symbol(:jlmol_measure_dihedral), Cint,
                 (UInt64, UInt64, UInt64, UInt64, UInt64, Ref{Cdouble}), structure.id,
                 UInt64(first_atom_id), UInt64(second_atom_id), UInt64(third_atom_id),
                 UInt64(fourth_atom_id), result)
    _check(code)
    return Float64(result[])
end
dihedral(first_atom_id::Integer, second_atom_id::Integer, third_atom_id::Integer,
         fourth_atom_id::Integer) =
    dihedral(active_structure(), first_atom_id, second_atom_id, third_atom_id, fourth_atom_id)

function radius_of_gyration(structure::ActiveStructure = active_structure())
    result = Ref{Cdouble}()
    code = ccall(_symbol(:jlmol_radius_of_gyration), Cint,
                 (UInt64, Ref{Cdouble}), structure.id, result)
    _check(code)
    return Float64(result[])
end

include("analysis.jl")
include("trajectory.jl")
include("conformations.jl")

end
