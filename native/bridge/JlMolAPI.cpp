#include "bridge/JlMolAPI.h"
#include "core/Analysis.h"
#include "core/MolecularScene.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <memory>
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

std::optional<jlmol::Vec3> atomPosition(const jlmol::MolecularScene* scene, std::uint64_t objectId,
                                        std::uint64_t atomId) {
    const auto object = scene ? scene->object(objectId) : std::nullopt;
    if (!object || !object->molecule) return std::nullopt;
    const auto& atoms = object->molecule->atoms();
    const auto found = std::find_if(atoms.begin(), atoms.end(),
                                    [atomId](const auto& atom) { return atom.id == atomId; });
    return found == atoms.end() ? std::nullopt : std::optional<jlmol::Vec3>{found->position};
}

bool validateResultPointer(double* result) {
    if (result) return true;
    lastError = "A non-null result pointer is required";
    return false;
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

size_t jlmol_object_count(void) {
    const auto* scene = sceneOrError();
    return scene ? scene->objectCount() : 0U;
}

uint64_t jlmol_object_id_at(size_t index) {
    const auto* scene = sceneOrError();
    const auto objects = scene ? scene->objects() : std::vector<jlmol::MolecularObject>{};
    if (index >= objects.size()) {
        lastError = "Object index is out of range";
        return 0;
    }
    return objects[index].id;
}

uint64_t jlmol_active_object_id(void) {
    const auto* scene = sceneOrError();
    return scene ? scene->activeObjectId().value_or(0) : 0;
}

uint64_t jlmol_load_structure(const char* path) {
    auto* scene = sceneOrError();
    if (!scene || !path || *path == '\0') {
        lastError = "A file path and active scene are required";
        return 0;
    }
    try {
        auto molecule = std::make_shared<jlmol::Molecule>(jlmol::Molecule::load(path));
        const auto id = scene->addMolecule(std::move(molecule));
        notifySceneChanged();
        return id;
    } catch (const std::exception& error) {
        lastError = error.what();
        return 0;
    }
}

int jlmol_close_object(uint64_t objectId) {
    auto* scene = sceneOrError();
    if (!scene || !scene->closeObject(objectId)) {
        lastError = "Molecular object does not exist";
        return 1;
    }
    notifySceneChanged();
    return 0;
}

int jlmol_set_active_object(uint64_t objectId) {
    auto* scene = sceneOrError();
    if (!scene || !scene->setActiveObject(objectId)) {
        lastError = "Molecular object does not exist";
        return 1;
    }
    notifySceneChanged();
    return 0;
}

const char* jlmol_object_name(uint64_t objectId) {
    const auto* scene = sceneOrError();
    const auto object = scene ? scene->object(objectId) : std::nullopt;
    if (!object || !object->molecule) {
        lastError = "Molecular object does not exist";
        return "";
    }
    returnString = object->molecule->name();
    return returnString.c_str();
}

size_t jlmol_object_atom_count(uint64_t objectId) {
    const auto* scene = sceneOrError();
    const auto object = scene ? scene->object(objectId) : std::nullopt;
    return object && object->molecule ? object->molecule->atoms().size() : 0U;
}

size_t jlmol_object_bond_count(uint64_t objectId) {
    const auto* scene = sceneOrError();
    const auto object = scene ? scene->object(objectId) : std::nullopt;
    return object && object->molecule ? object->molecule->bonds().size() : 0U;
}

int jlmol_copy_object_atoms(uint64_t objectId, double* coordinates, uint64_t* atomIds, size_t capacity) {
    const auto* scene = sceneOrError();
    if (!scene) return 1;
    const auto object = scene->object(objectId);
    if (!object || !object->molecule) {
        lastError = "Molecular object does not exist";
        return 2;
    }
    const auto& atoms = object->molecule->atoms();
    if (capacity < atoms.size() || (!atoms.empty() && (!coordinates || !atomIds))) {
        lastError = "Atom snapshot buffers are null or too small";
        return 3;
    }
    for (size_t index = 0; index < atoms.size(); ++index) {
        const auto& atom = atoms[index];
        coordinates[index * 3] = atom.position.x;
        coordinates[index * 3 + 1] = atom.position.y;
        coordinates[index * 3 + 2] = atom.position.z;
        atomIds[index] = atom.id;
    }
    return 0;
}

const char* jlmol_object_atom_field(uint64_t objectId, size_t index, const char* field) {
    const auto* scene = sceneOrError();
    const auto object = scene ? scene->object(objectId) : std::nullopt;
    if (!object || !object->molecule || index >= object->molecule->atoms().size() || !field) {
        lastError = "Molecular object, atom index, or field is invalid";
        return "";
    }
    const auto& atom = object->molecule->atoms()[index];
    if (std::string(field) == "name") {
        returnString = atom.name;
    } else if (std::string(field) == "identity") {
        // Length prefixes prevent collisions when chain or atom names contain separators.
        const auto part = [](const std::string& value) {
            return std::to_string(value.size()) + ":" + value;
        };
        returnString = part(std::to_string(atom.modelId)) + part(atom.chainId) +
                       part(std::to_string(atom.residueId)) + part(atom.residueName) +
                       part(atom.name) + part(std::string(1, atom.alternateLocation));
    } else {
        lastError = "Unsupported atom field";
        return "";
    }
    return returnString.c_str();
}

int jlmol_object_visible(uint64_t objectId) {
    const auto* scene = sceneOrError();
    return scene && scene->objectVisible(objectId) ? 1 : 0;
}

int jlmol_set_object_visible(uint64_t objectId, int visible) {
    auto* scene = sceneOrError();
    if (!scene || !scene->setObjectVisible(objectId, visible != 0)) {
        lastError = "Molecular object does not exist";
        return 1;
    }
    notifySceneChanged();
    return 0;
}

int jlmol_object_opacity(uint64_t objectId, double* result) {
    const auto* scene = sceneOrError();
    if (!scene || !validateResultPointer(result)) return 1;
    const auto opacity = scene->objectOpacity(objectId);
    if (!opacity) {
        lastError = "Molecular object does not exist";
        return 2;
    }
    *result = *opacity;
    return 0;
}

int jlmol_set_object_opacity(uint64_t objectId, double opacity) {
    auto* scene = sceneOrError();
    if (!scene) return 1;
    if (!std::isfinite(opacity) || opacity < 0.0 || opacity > 1.0) {
        lastError = "Opacity must be finite and between 0 and 1";
        return 2;
    }
    if (!scene->setObjectOpacity(objectId, opacity)) {
        lastError = "Molecular object does not exist";
        return 3;
    }
    notifySceneChanged();
    return 0;
}

int jlmol_attach_trajectory(uint64_t objectId, const char* path) {
    auto* scene = sceneOrError();
    if (!scene) return 1;
    if (!path || !*path) {
        lastError = "A trajectory file path is required";
        return 2;
    }
    try {
        scene->attachTrajectory(objectId, path);
        notifySceneChanged();
        return 0;
    } catch (const std::exception& error) {
        lastError = error.what();
        return 2;
    }
}

int jlmol_set_trajectory_frame(uint64_t objectId, size_t index) {
    auto* scene = sceneOrError();
    if (!scene) return 1;
    try {
        scene->setTrajectoryFrame(objectId, index);
        notifySceneChanged();
        return 0;
    } catch (const std::exception& error) {
        lastError = error.what();
        return 2;
    }
}

int jlmol_trajectory_state(uint64_t objectId, size_t* count, size_t* index, double* timePs, double* cell) {
    const auto* scene = sceneOrError();
    if (!scene) return 1;
    if (!count || !index || !timePs || !cell) {
        lastError = "Trajectory state output buffers must not be null";
        return 2;
    }
    const auto object = scene->object(objectId);
    if (!object) {
        lastError = "Molecular object does not exist";
        return 2;
    }
    *count = object->trajectory ? object->trajectory->frameCount() : 0;
    *index = object->frameIndex;
    *timePs = object->frameMetadata.timePs.value_or(std::numeric_limits<double>::quiet_NaN());
    std::copy(object->frameMetadata.cell.begin(), object->frameMetadata.cell.end(), cell);
    return 0;
}

int jlmol_copy_trajectory_frame(uint64_t objectId, size_t index, double* coordinates, size_t capacity) {
    const auto* scene = sceneOrError();
    if (!scene) return 1;
    const auto object = scene->object(objectId);
    if (!object || !object->trajectory) {
        lastError = "No trajectory is attached to this structure";
        return 2;
    }
    if (!coordinates || capacity < object->molecule->atoms().size()) {
        lastError = "Trajectory coordinate buffer is null or too small";
        return 2;
    }
    try {
        const auto frame = object->trajectory->readFrame(index);
        for (const auto& atom : frame->positions) {
            *coordinates++ = atom.x;
            *coordinates++ = atom.y;
            *coordinates++ = atom.z;
        }
        return 0;
    } catch (const std::exception& error) {
        lastError = error.what();
        return 2;
    }
}

int jlmol_set_object_representation(uint64_t objectId, const char* representation) {
    auto* scene = sceneOrError();
    const auto parsed = representation ? jlmol::representationFromString(representation) : std::nullopt;
    if (!scene || !parsed || !scene->setRepresentation(objectId, *parsed)) {
        lastError = parsed ? "Molecular object does not exist" : "Unsupported representation";
        return 1;
    }
    notifySceneChanged();
    return 0;
}

int jlmol_set_object_color_scheme(uint64_t objectId, const char* colorScheme) {
    auto* scene = sceneOrError();
    const auto parsed = colorScheme ? jlmol::colorSchemeFromString(colorScheme) : std::nullopt;
    if (!scene || !parsed || !scene->setColorScheme(objectId, *parsed)) {
        lastError = parsed ? "Molecular object does not exist" : "Unsupported color scheme";
        return 1;
    }
    notifySceneChanged();
    return 0;
}

const char* jlmol_object_representation(uint64_t objectId) {
    const auto* scene = sceneOrError();
    const auto value = scene ? scene->representation(objectId) : std::nullopt;
    returnString = value ? jlmol::toString(*value) : "";
    return returnString.c_str();
}

const char* jlmol_object_color_scheme(uint64_t objectId) {
    const auto* scene = sceneOrError();
    const auto value = scene ? scene->colorScheme(objectId) : std::nullopt;
    returnString = value ? jlmol::toString(*value) : "";
    return returnString.c_str();
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

uint64_t jlmol_selected_object_id(void) {
    const auto* scene = sceneOrError();
    return scene ? scene->selectedObjectId().value_or(0U) : 0U;
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

int jlmol_measure_distance(uint64_t objectId, uint64_t firstAtomId, uint64_t secondAtomId, double* result) {
    const auto* scene = sceneOrError();
    if (!scene || !validateResultPointer(result)) return 1;
    const auto first = atomPosition(scene, objectId, firstAtomId);
    const auto second = atomPosition(scene, objectId, secondAtomId);
    if (!first || !second) {
        lastError = "One or more atom identifiers do not exist in the molecular object";
        return 2;
    }
    *result = jlmol::analysis::distance(*first, *second);
    return 0;
}

int jlmol_measure_angle(uint64_t objectId, uint64_t firstAtomId, uint64_t vertexAtomId,
                        uint64_t thirdAtomId, double* result) {
    const auto* scene = sceneOrError();
    if (!scene || !validateResultPointer(result)) return 1;
    const auto first = atomPosition(scene, objectId, firstAtomId);
    const auto vertex = atomPosition(scene, objectId, vertexAtomId);
    const auto third = atomPosition(scene, objectId, thirdAtomId);
    if (!first || !vertex || !third) {
        lastError = "One or more atom identifiers do not exist in the molecular object";
        return 2;
    }
    const auto value = jlmol::analysis::angleDegrees(*first, *vertex, *third);
    if (!value) {
        lastError = "Angle is undefined for coincident atom positions";
        return 3;
    }
    *result = *value;
    return 0;
}

int jlmol_measure_dihedral(uint64_t objectId, uint64_t firstAtomId, uint64_t secondAtomId,
                           uint64_t thirdAtomId, uint64_t fourthAtomId, double* result) {
    const auto* scene = sceneOrError();
    if (!scene || !validateResultPointer(result)) return 1;
    const auto first = atomPosition(scene, objectId, firstAtomId);
    const auto second = atomPosition(scene, objectId, secondAtomId);
    const auto third = atomPosition(scene, objectId, thirdAtomId);
    const auto fourth = atomPosition(scene, objectId, fourthAtomId);
    if (!first || !second || !third || !fourth) {
        lastError = "One or more atom identifiers do not exist in the molecular object";
        return 2;
    }
    const auto value = jlmol::analysis::dihedralDegrees(*first, *second, *third, *fourth);
    if (!value) {
        lastError = "Dihedral is undefined for coincident or collinear atom positions";
        return 3;
    }
    *result = *value;
    return 0;
}

int jlmol_radius_of_gyration(uint64_t objectId, double* result) {
    const auto* scene = sceneOrError();
    if (!scene || !validateResultPointer(result)) return 1;
    const auto object = scene->object(objectId);
    if (!object || !object->molecule) {
        lastError = "Molecular object does not exist";
        return 2;
    }
    *result = jlmol::analysis::radiusOfGyration(*object->molecule);
    return 0;
}

const char* jlmol_get_last_error(void) { return lastError.c_str(); }

} // extern "C"

