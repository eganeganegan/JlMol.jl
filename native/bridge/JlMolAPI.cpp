#include "bridge/JlMolAPI.h"
#include "core/MolecularScene.h"

#include <mutex>
#include <string>

namespace {

std::mutex apiMutex;
jlmol::MolecularScene* activeScene = nullptr;
jlmol_scene_changed_callback sceneChangedCallback = nullptr;
void* sceneChangedUserData = nullptr;
thread_local std::string lastError;
thread_local std::string returnString;

jlmol::MolecularScene* sceneOrError() {
    std::scoped_lock lock(apiMutex);
    if (!activeScene) {
        lastError = "No active JlMol scene";
    }
    return activeScene;
}

void notifySceneChanged() {
    jlmol_scene_changed_callback callback;
    void* userData;
    {
        std::scoped_lock lock(apiMutex);
        callback = sceneChangedCallback;
        userData = sceneChangedUserData;
    }
    if (callback) {
        callback(userData);
    }
}

template <typename Getter>
const char* selectedString(Getter getter) {
    const auto* scene = sceneOrError();
    const auto atom = scene ? scene->selectedAtom() : std::nullopt;
    if (!atom) {
        lastError = "No atom is selected";
        return "";
    }
    returnString = getter(*atom);
    return returnString.c_str();
}

} // namespace

extern "C" {

int jlmol_api_version(void) { return JLMOL_API_VERSION; }

void jlmol_set_active_scene(void* scene) {
    std::scoped_lock lock(apiMutex);
    activeScene = static_cast<jlmol::MolecularScene*>(scene);
}

void jlmol_set_scene_changed_callback(jlmol_scene_changed_callback callback, void* userData) {
    std::scoped_lock lock(apiMutex);
    sceneChangedCallback = callback;
    sceneChangedUserData = userData;
}

size_t jlmol_active_atom_count(void) {
    const auto* scene = sceneOrError();
    const auto molecule = scene ? scene->molecule() : nullptr;
    return molecule ? molecule->atoms().size() : 0U;
}

size_t jlmol_active_bond_count(void) {
    const auto* scene = sceneOrError();
    const auto molecule = scene ? scene->molecule() : nullptr;
    return molecule ? molecule->bonds().size() : 0U;
}

int jlmol_set_active_representation(const char* representation) {
    auto* scene = sceneOrError();
    if (!scene || !representation) {
        lastError = "Representation and active scene are required";
        return 1;
    }
    const auto parsed = jlmol::representationFromString(representation);
    if (!parsed) {
        lastError = std::string("Unsupported representation: ") + representation;
        return 2;
    }
    scene->setRepresentation(*parsed);
    notifySceneChanged();
    return 0;
}

int jlmol_set_active_color_scheme(const char* colorScheme) {
    auto* scene = sceneOrError();
    if (!scene || !colorScheme) {
        lastError = "Color scheme and active scene are required";
        return 1;
    }
    const auto parsed = jlmol::colorSchemeFromString(colorScheme);
    if (!parsed) {
        lastError = std::string("Unsupported color scheme: ") + colorScheme;
        return 2;
    }
    scene->setColorScheme(*parsed);
    notifySceneChanged();
    return 0;
}

const char* jlmol_active_representation(void) {
    const auto* scene = sceneOrError();
    returnString = scene ? jlmol::toString(scene->representation()) : "";
    return returnString.c_str();
}

const char* jlmol_active_color_scheme(void) {
    const auto* scene = sceneOrError();
    returnString = scene ? jlmol::toString(scene->colorScheme()) : "";
    return returnString.c_str();
}

int jlmol_has_selection(void) {
    const auto* scene = sceneOrError();
    return scene && scene->selectedAtomId().has_value() ? 1 : 0;
}

uint64_t jlmol_selected_atom_id(void) {
    const auto* scene = sceneOrError();
    return scene ? scene->selectedAtomId().value_or(0U) : 0U;
}

int jlmol_selected_atom_serial(void) {
    const auto* scene = sceneOrError();
    const auto atom = scene ? scene->selectedAtom() : std::nullopt;
    return atom ? atom->serial : 0;
}

int jlmol_selected_atom_residue_id(void) {
    const auto* scene = sceneOrError();
    const auto atom = scene ? scene->selectedAtom() : std::nullopt;
    return atom ? atom->residueId : 0;
}

const char* jlmol_selected_atom_name(void) { return selectedString([](const auto& atom) { return atom.name; }); }
const char* jlmol_selected_atom_element(void) { return selectedString([](const auto& atom) { return atom.element; }); }
const char* jlmol_selected_atom_residue_name(void) { return selectedString([](const auto& atom) { return atom.residueName; }); }
const char* jlmol_selected_atom_chain(void) { return selectedString([](const auto& atom) { return atom.chainId; }); }

const char* jlmol_get_last_error(void) { return lastError.c_str(); }

} // extern "C"

