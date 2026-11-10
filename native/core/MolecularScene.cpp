#include "core/MolecularScene.h"

#include <cmath>

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace jlmol {

void MolecularScene::setMolecule(std::shared_ptr<Molecule> molecule) {
    std::scoped_lock lock(mutex_);
    objects_.clear();
    activeObjectId_.reset();
    selectedObjectId_.reset();
    selectedAtomIds_.clear();
    if (molecule) {
        const auto id = nextObjectId_++;
        objects_.push_back(MolecularObject{id, std::move(molecule), true, defaultRepresentation_, defaultColorScheme_});
        activeObjectId_ = id;
    }
    ++generation_;
}

ObjectId MolecularScene::addMolecule(std::shared_ptr<Molecule> molecule) {
    if (!molecule) return 0;
    std::scoped_lock lock(mutex_);
    const auto id = nextObjectId_++;
    objects_.push_back(MolecularObject{id, std::move(molecule), true, defaultRepresentation_, defaultColorScheme_});
    activeObjectId_ = id;
    ++generation_;
    return id;
}

bool MolecularScene::closeObject(ObjectId id) {
    std::scoped_lock lock(mutex_);
    const auto found = findObject(id);
    if (found == objects_.end()) return false;
    objects_.erase(found);
    if (selectedObjectId_ == id) {
        selectedObjectId_.reset();
        selectedAtomIds_.clear();
    }
    if (activeObjectId_ == id) {
        activeObjectId_ = objects_.empty() ? std::nullopt : std::optional<ObjectId>{objects_.back().id};
    }
    ++generation_;
    return true;
}

void MolecularScene::attachTrajectory(ObjectId id, const std::filesystem::path& path) {
    const auto original = object(id);
    if (!original || !original->molecule) throw std::runtime_error("Molecular object does not exist");
    auto trajectory = std::make_shared<Trajectory>(path, original->molecule->atoms().size());
    const auto frame = trajectory->readFrame(0);
    auto molecule = std::make_shared<Molecule>(original->molecule->withPositions(frame->positions));
    std::scoped_lock lock(mutex_);
    const auto found = findObject(id);
    if (found == objects_.end() || found->molecule != original->molecule) {
        throw std::runtime_error("The structure changed while loading the trajectory");
    }
    found->molecule = std::move(molecule);
    found->trajectory = std::move(trajectory);
    found->frameIndex = 0;
    found->frameMetadata = frame->metadata;
    ++generation_;
}

void MolecularScene::setTrajectoryFrame(ObjectId id, std::size_t index) {
    const auto original = object(id);
    if (!original || !original->trajectory) throw std::runtime_error("No trajectory is attached to this structure");
    if (index == original->frameIndex) return;
    const auto frame = original->trajectory->readFrame(index);
    auto molecule = std::make_shared<Molecule>(original->molecule->withPositions(frame->positions));
    std::scoped_lock lock(mutex_);
    const auto found = findObject(id);
    if (found == objects_.end() || found->molecule != original->molecule) {
        throw std::runtime_error("The structure changed while reading the trajectory frame");
    }
    found->molecule = std::move(molecule);
    found->frameIndex = index;
    found->frameMetadata = frame->metadata;
    ++generation_;
}

std::vector<MolecularObject> MolecularScene::objects() const {
    std::scoped_lock lock(mutex_);
    return objects_;
}

std::optional<MolecularObject> MolecularScene::object(ObjectId id) const {
    std::scoped_lock lock(mutex_);
    const auto found = findObject(id);
    return found == objects_.end() ? std::nullopt : std::optional<MolecularObject>{*found};
}

std::size_t MolecularScene::objectCount() const {
    std::scoped_lock lock(mutex_);
    return objects_.size();
}

bool MolecularScene::setActiveObject(ObjectId id) {
    std::scoped_lock lock(mutex_);
    if (findObject(id) == objects_.end()) return false;
    if (activeObjectId_ != id) {
        activeObjectId_ = id;
        ++generation_;
    }
    return true;
}

std::optional<ObjectId> MolecularScene::activeObjectId() const {
    std::scoped_lock lock(mutex_);
    return activeObjectId_;
}

std::shared_ptr<const Molecule> MolecularScene::molecule() const {
    std::scoped_lock lock(mutex_);
    if (!activeObjectId_) return nullptr;
    const auto found = findObject(*activeObjectId_);
    return found == objects_.end() ? nullptr : found->molecule;
}

bool MolecularScene::setObjectVisible(ObjectId id, bool visible) {
    std::scoped_lock lock(mutex_);
    const auto found = findObject(id);
    if (found == objects_.end()) return false;
    if (found->visible != visible) {
        found->visible = visible;
        ++generation_;
    }
    return true;
}

bool MolecularScene::objectVisible(ObjectId id) const {
    std::scoped_lock lock(mutex_);
    const auto found = findObject(id);
    return found != objects_.end() && found->visible;
}

bool MolecularScene::setObjectOpacity(ObjectId id, double opacity) {
    if (!std::isfinite(opacity) || opacity < 0.0 || opacity > 1.0) return false;
    std::scoped_lock lock(mutex_);
    const auto found = findObject(id);
    if (found == objects_.end()) return false;
    if (found->opacity != opacity) {
        found->opacity = opacity;
        ++generation_;
    }
    return true;
}

std::optional<double> MolecularScene::objectOpacity(ObjectId id) const {
    std::scoped_lock lock(mutex_);
    const auto found = findObject(id);
    return found == objects_.end() ? std::nullopt : std::optional<double>{found->opacity};
}

void MolecularScene::setRepresentation(Representation representation) {
    std::scoped_lock lock(mutex_);
    defaultRepresentation_ = representation;
    if (activeObjectId_) {
        const auto found = findObject(*activeObjectId_);
        if (found != objects_.end()) found->representation = representation;
    }
    ++generation_;
}

bool MolecularScene::setRepresentation(ObjectId id, Representation representation) {
    std::scoped_lock lock(mutex_);
    const auto found = findObject(id);
    if (found == objects_.end()) return false;
    found->representation = representation;
    ++generation_;
    return true;
}

Representation MolecularScene::representation() const {
    std::scoped_lock lock(mutex_);
    if (activeObjectId_) {
        const auto found = findObject(*activeObjectId_);
        if (found != objects_.end()) return found->representation;
    }
    return defaultRepresentation_;
}

std::optional<Representation> MolecularScene::representation(ObjectId id) const {
    std::scoped_lock lock(mutex_);
    const auto found = findObject(id);
    return found == objects_.end() ? std::nullopt : std::optional<Representation>{found->representation};
}

void MolecularScene::setColorScheme(ColorScheme scheme) {
    std::scoped_lock lock(mutex_);
    defaultColorScheme_ = scheme;
    if (activeObjectId_) {
        const auto found = findObject(*activeObjectId_);
        if (found != objects_.end()) found->colorScheme = scheme;
    }
    ++generation_;
}

bool MolecularScene::setColorScheme(ObjectId id, ColorScheme scheme) {
    std::scoped_lock lock(mutex_);
    const auto found = findObject(id);
    if (found == objects_.end()) return false;
    found->colorScheme = scheme;
    ++generation_;
    return true;
}

ColorScheme MolecularScene::colorScheme() const {
    std::scoped_lock lock(mutex_);
    if (activeObjectId_) {
        const auto found = findObject(*activeObjectId_);
        if (found != objects_.end()) return found->colorScheme;
    }
    return defaultColorScheme_;
}

std::optional<ColorScheme> MolecularScene::colorScheme(ObjectId id) const {
    std::scoped_lock lock(mutex_);
    const auto found = findObject(id);
    return found == objects_.end() ? std::nullopt : std::optional<ColorScheme>{found->colorScheme};
}

void MolecularScene::selectAtom(std::optional<std::uint64_t> atomId) {
    std::scoped_lock lock(mutex_);
    selectedObjectId_ = atomId ? activeObjectId_ : std::nullopt;
    selectedAtomIds_ = atomId ? std::vector<std::uint64_t>{*atomId} : std::vector<std::uint64_t>{};
    ++generation_;
}

void MolecularScene::selectAtom(ObjectId objectId, std::optional<std::uint64_t> atomId) {
    std::scoped_lock lock(mutex_);
    if (atomId && findObject(objectId) != objects_.end()) {
        selectedObjectId_ = objectId;
        selectedAtomIds_ = {*atomId};
        activeObjectId_ = objectId;
    } else {
        selectedObjectId_.reset();
        selectedAtomIds_.clear();
    }
    ++generation_;
}

void MolecularScene::extendAtomSelection(ObjectId objectId, std::uint64_t atomId) {
    std::scoped_lock lock(mutex_);
    if (findObject(objectId) == objects_.end()) return;
    if (selectedObjectId_ != objectId) {
        selectedObjectId_ = objectId;
        selectedAtomIds_.clear();
    }
    const auto existing = std::find(selectedAtomIds_.begin(), selectedAtomIds_.end(), atomId);
    if (existing != selectedAtomIds_.end()) {
        selectedAtomIds_.erase(existing);
    } else {
        if (selectedAtomIds_.size() >= 4) selectedAtomIds_.erase(selectedAtomIds_.begin());
        selectedAtomIds_.push_back(atomId);
    }
    if (selectedAtomIds_.empty()) selectedObjectId_.reset();
    activeObjectId_ = objectId;
    ++generation_;
}

void MolecularScene::selectAtoms(ObjectId objectId, std::vector<std::uint64_t> atomIds) {
    std::scoped_lock lock(mutex_);
    const auto object = findObject(objectId);
    if (object == objects_.end() || !object->molecule) return;
    atomIds.erase(std::remove_if(atomIds.begin(), atomIds.end(), [&object](std::uint64_t id) {
        return std::none_of(object->molecule->atoms().begin(), object->molecule->atoms().end(),
                            [id](const Atom& atom) { return atom.id == id; });
    }), atomIds.end());
    selectedObjectId_ = atomIds.empty() ? std::nullopt : std::optional<ObjectId>{objectId};
    selectedAtomIds_ = std::move(atomIds);
    activeObjectId_ = objectId;
    ++generation_;
}

std::optional<ObjectId> MolecularScene::selectedObjectId() const {
    std::scoped_lock lock(mutex_);
    return selectedObjectId_;
}

std::optional<std::uint64_t> MolecularScene::selectedAtomId() const {
    std::scoped_lock lock(mutex_);
    return selectedAtomIds_.empty() ? std::nullopt : std::optional<std::uint64_t>{selectedAtomIds_.back()};
}

std::vector<std::uint64_t> MolecularScene::selectedAtomIds() const {
    std::scoped_lock lock(mutex_);
    return selectedAtomIds_;
}

std::optional<Atom> MolecularScene::selectedAtom() const {
    std::scoped_lock lock(mutex_);
    if (!selectedObjectId_ || selectedAtomIds_.empty()) return std::nullopt;
    const auto object = findObject(*selectedObjectId_);
    if (object == objects_.end() || !object->molecule) return std::nullopt;
    const auto found = std::find_if(object->molecule->atoms().begin(), object->molecule->atoms().end(),
                                    [this](const Atom& atom) { return atom.id == selectedAtomIds_.back(); });
    return found == object->molecule->atoms().end() ? std::nullopt : std::optional<Atom>{*found};
}

std::vector<Atom> MolecularScene::selectedAtoms() const {
    std::scoped_lock lock(mutex_);
    std::vector<Atom> result;
    if (!selectedObjectId_ || selectedAtomIds_.empty()) return result;
    const auto object = findObject(*selectedObjectId_);
    if (object == objects_.end() || !object->molecule) return result;
    result.reserve(selectedAtomIds_.size());
    for (const auto id : selectedAtomIds_) {
        const auto found = std::find_if(object->molecule->atoms().begin(), object->molecule->atoms().end(),
                                        [id](const Atom& atom) { return atom.id == id; });
        if (found != object->molecule->atoms().end()) result.push_back(*found);
    }
    return result;
}

std::uint64_t MolecularScene::generation() const {
    std::scoped_lock lock(mutex_);
    return generation_;
}

std::string toString(Representation representation) {
    switch (representation) {
    case Representation::BallAndStick: return "ball_and_stick";
    case Representation::SpaceFilling: return "spacefill";
    case Representation::Points: return "points";
    case Representation::Lines: return "lines";
    case Representation::CAlphaTrace: return "c_alpha_trace";
    case Representation::Backbone: return "backbone";
    case Representation::Cartoon: return "cartoon";
    case Representation::Ribbon: return "ribbon";
    }
    return "unknown";
}

std::optional<Representation> representationFromString(const std::string& value) {
    std::string key = value;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return c == '-' ? '_' : static_cast<char>(std::tolower(c));
    });
    if (key == "ball_and_stick" || key == "ballstick") return Representation::BallAndStick;
    if (key == "spacefill" || key == "space_filling" || key == "cpk") return Representation::SpaceFilling;
    if (key == "points") return Representation::Points;
    if (key == "lines") return Representation::Lines;
    if (key == "c_alpha_trace" || key == "calpha" || key == "trace") return Representation::CAlphaTrace;
    if (key == "backbone") return Representation::Backbone;
    if (key == "cartoon") return Representation::Cartoon;
    if (key == "ribbon") return Representation::Ribbon;
    return std::nullopt;
}

std::string toString(ColorScheme scheme) {
    switch (scheme) {
    case ColorScheme::Element: return "element";
    case ColorScheme::Chain: return "chain";
    case ColorScheme::Residue: return "residue";
    case ColorScheme::SecondaryStructure: return "secondary_structure";
    }
    return "unknown";
}

std::optional<ColorScheme> colorSchemeFromString(const std::string& value) {
    std::string key = value;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return c == '-' ? '_' : static_cast<char>(std::tolower(c));
    });
    if (key == "element" || key == "cpk") return ColorScheme::Element;
    if (key == "chain") return ColorScheme::Chain;
    if (key == "residue") return ColorScheme::Residue;
    if (key == "secondary_structure" || key == "secondary") return ColorScheme::SecondaryStructure;
    return std::nullopt;
}

} // namespace jlmol
