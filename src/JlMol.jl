module JlMol

import Libdl

export ActiveStructure, active_structure, atom_count, bond_count, representation,
       setrepresentation!, color_scheme, colorby!, selected_atom

"A safe reference to the application-owned active molecular structure."
struct ActiveStructure end

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

function _check(code::Integer)
    code == 0 && return nothing
    message = unsafe_string(ccall(_symbol(:jlmol_get_last_error), Cstring, ()))
    error(isempty(message) ? "JlMol native operation failed with code $code" : message)
end

active_structure() = ActiveStructure()
atom_count(::ActiveStructure = ActiveStructure()) = Int(ccall(_symbol(:jlmol_active_atom_count), Csize_t, ()))
bond_count(::ActiveStructure = ActiveStructure()) = Int(ccall(_symbol(:jlmol_active_bond_count), Csize_t, ()))
representation(::ActiveStructure = ActiveStructure()) = Symbol(unsafe_string(ccall(_symbol(:jlmol_active_representation), Cstring, ())))
color_scheme(::ActiveStructure = ActiveStructure()) = Symbol(unsafe_string(ccall(_symbol(:jlmol_active_color_scheme), Cstring, ())))

function setrepresentation!(::ActiveStructure, value::Symbol)
    _check(ccall(_symbol(:jlmol_set_active_representation), Cint, (Cstring,), string(value)))
    return value
end

setrepresentation!(value::Symbol) = setrepresentation!(ActiveStructure(), value)

function colorby!(::ActiveStructure, value::Symbol)
    _check(ccall(_symbol(:jlmol_set_active_color_scheme), Cint, (Cstring,), string(value)))
    return value
end

colorby!(value::Symbol) = colorby!(ActiveStructure(), value)

function selected_atom()
    ccall(_symbol(:jlmol_has_selection), Cint, ()) == 0 && return nothing
    return (
        id = UInt64(ccall(_symbol(:jlmol_selected_atom_id), UInt64, ())),
        serial = Int(ccall(_symbol(:jlmol_selected_atom_serial), Cint, ())),
        name = unsafe_string(ccall(_symbol(:jlmol_selected_atom_name), Cstring, ())),
        element = unsafe_string(ccall(_symbol(:jlmol_selected_atom_element), Cstring, ())),
        residue_name = unsafe_string(ccall(_symbol(:jlmol_selected_atom_residue_name), Cstring, ())),
        residue_id = Int(ccall(_symbol(:jlmol_selected_atom_residue_id), Cint, ())),
        chain = unsafe_string(ccall(_symbol(:jlmol_selected_atom_chain), Cstring, ())),
    )
end

end
