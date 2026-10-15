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

#define JLMOL_API_VERSION 1

typedef void (*jlmol_scene_changed_callback)(void* user_data);

JLMOL_API int jlmol_api_version(void);
JLMOL_API void jlmol_set_active_scene(void* scene);
JLMOL_API void jlmol_set_scene_changed_callback(jlmol_scene_changed_callback callback, void* user_data);
JLMOL_API size_t jlmol_active_atom_count(void);
JLMOL_API size_t jlmol_active_bond_count(void);
JLMOL_API int jlmol_set_active_representation(const char* representation);
JLMOL_API int jlmol_set_active_color_scheme(const char* color_scheme);
JLMOL_API const char* jlmol_active_representation(void);
JLMOL_API const char* jlmol_active_color_scheme(void);
JLMOL_API int jlmol_has_selection(void);
JLMOL_API uint64_t jlmol_selected_atom_id(void);
JLMOL_API int jlmol_selected_atom_serial(void);
JLMOL_API int jlmol_selected_atom_residue_id(void);
JLMOL_API const char* jlmol_selected_atom_name(void);
JLMOL_API const char* jlmol_selected_atom_element(void);
JLMOL_API const char* jlmol_selected_atom_residue_name(void);
JLMOL_API const char* jlmol_selected_atom_chain(void);
JLMOL_API const char* jlmol_get_last_error(void);

#ifdef __cplusplus
}
#endif

