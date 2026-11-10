#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(JLMOL_CORE_BUILD)
#    define JLMOL_API __declspec(dllexport)
#  else
#    define JLMOL_API __declspec(dllimport)
#  endif
#else
#  define JLMOL_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define JLMOL_API_VERSION 7

typedef void (*jlmol_scene_changed_callback)(void* user_data);

JLMOL_API int jlmol_api_version(void);
JLMOL_API void jlmol_set_active_scene(void* scene);
JLMOL_API void jlmol_set_scene_changed_callback(jlmol_scene_changed_callback callback, void* user_data);
JLMOL_API size_t jlmol_active_atom_count(void);
JLMOL_API size_t jlmol_active_bond_count(void);
JLMOL_API size_t jlmol_object_count(void);
JLMOL_API uint64_t jlmol_object_id_at(size_t index);
JLMOL_API uint64_t jlmol_active_object_id(void);
JLMOL_API uint64_t jlmol_load_structure(const char* path);
JLMOL_API int jlmol_close_object(uint64_t object_id);
JLMOL_API int jlmol_set_active_object(uint64_t object_id);
JLMOL_API const char* jlmol_object_name(uint64_t object_id);
JLMOL_API size_t jlmol_object_atom_count(uint64_t object_id);
JLMOL_API size_t jlmol_object_bond_count(uint64_t object_id);
/* Copies xyz triples and stable IDs into caller-owned arrays. Capacity is in atoms. */
JLMOL_API int jlmol_copy_object_atoms(uint64_t object_id, double* coordinates,
                                     uint64_t* atom_ids, size_t capacity);
/* Supported fields: name, identity. Returned strings follow the usual thread-local lifetime. */
JLMOL_API const char* jlmol_object_atom_field(uint64_t object_id, size_t index, const char* field);
JLMOL_API int jlmol_object_visible(uint64_t object_id);
JLMOL_API int jlmol_set_object_visible(uint64_t object_id, int visible);
/* Opacity is finite and in [0, 1]. Visibility remains a separate property. */
JLMOL_API int jlmol_object_opacity(uint64_t object_id, double* result);
JLMOL_API int jlmol_set_object_opacity(uint64_t object_id, double opacity);
/* Frame indices are zero-based. Cell has nine doubles (three basis vectors). */
JLMOL_API int jlmol_attach_trajectory(uint64_t object_id, const char* path);
JLMOL_API int jlmol_set_trajectory_frame(uint64_t object_id, size_t index);
/* A static structure returns count 0. Missing time is NaN; no cell is all zeros. */
JLMOL_API int jlmol_trajectory_state(uint64_t object_id, size_t* count, size_t* index,
                                    double* time_ps, double* cell);
/* Copy xyz triples without changing the displayed frame. Capacity is in atoms. */
JLMOL_API int jlmol_copy_trajectory_frame(uint64_t object_id, size_t index,
                                         double* coordinates, size_t capacity);
JLMOL_API int jlmol_set_object_representation(uint64_t object_id, const char* representation);
JLMOL_API int jlmol_set_object_color_scheme(uint64_t object_id, const char* color_scheme);
/* Copies RGB triples in topology order. component_count counts floats, not atoms. */
JLMOL_API int jlmol_set_object_atom_colors(uint64_t object_id, const float* rgb, size_t component_count);
JLMOL_API const char* jlmol_object_representation(uint64_t object_id);
JLMOL_API const char* jlmol_object_color_scheme(uint64_t object_id);
JLMOL_API int jlmol_set_active_representation(const char* representation);
JLMOL_API int jlmol_set_active_color_scheme(const char* color_scheme);
JLMOL_API const char* jlmol_active_representation(void);
JLMOL_API const char* jlmol_active_color_scheme(void);
JLMOL_API int jlmol_has_selection(void);
JLMOL_API uint64_t jlmol_selected_object_id(void);
JLMOL_API uint64_t jlmol_selected_atom_id(void);
JLMOL_API int jlmol_selected_atom_serial(void);
JLMOL_API int jlmol_selected_atom_residue_id(void);
JLMOL_API const char* jlmol_selected_atom_name(void);
JLMOL_API const char* jlmol_selected_atom_element(void);
JLMOL_API const char* jlmol_selected_atom_residue_name(void);
JLMOL_API const char* jlmol_selected_atom_chain(void);
JLMOL_API int jlmol_measure_distance(uint64_t object_id, uint64_t first_atom_id,
                                     uint64_t second_atom_id, double* result);
JLMOL_API int jlmol_measure_angle(uint64_t object_id, uint64_t first_atom_id,
                                  uint64_t vertex_atom_id, uint64_t third_atom_id, double* result);
JLMOL_API int jlmol_measure_dihedral(uint64_t object_id, uint64_t first_atom_id,
                                     uint64_t second_atom_id, uint64_t third_atom_id,
                                     uint64_t fourth_atom_id, double* result);
JLMOL_API int jlmol_radius_of_gyration(uint64_t object_id, double* result);
JLMOL_API const char* jlmol_get_last_error(void);

#ifdef __cplusplus
}
#endif

