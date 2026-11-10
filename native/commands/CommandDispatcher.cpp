#include "commands/CommandDispatcher.h"

#include <QString>
#include <exception>

namespace jlmol {

CommandDispatcher::CommandDispatcher(std::shared_ptr<MolecularScene> scene, QObject* parent)
    : QObject(parent), scene_(std::move(scene)) {}

bool CommandDispatcher::loadStructure(const std::filesystem::path& path) {
    try {
        auto molecule = std::make_shared<Molecule>(Molecule::load(path));
        (void)scene_->addMolecule(std::move(molecule));
        emit sceneChanged();
        return true;
    } catch (const std::exception& error) {
        emit commandFailed(QString::fromUtf8(error.what()));
        return false;
    }
}

bool CommandDispatcher::closeObject(ObjectId id) {
    if (!scene_->closeObject(id)) return false;
    emit sceneChanged();
    return true;
}

bool CommandDispatcher::attachTrajectory(ObjectId id, const std::filesystem::path& path) {
    try {
        scene_->attachTrajectory(id, path);
        emit sceneChanged();
        return true;
    } catch (const std::exception& error) {
        emit commandFailed(QString::fromUtf8(error.what()));
        return false;
    }
}

bool CommandDispatcher::setTrajectoryFrame(ObjectId id, std::size_t index) {
    try {
        scene_->setTrajectoryFrame(id, index);
        emit frameChanged();
        return true;
    } catch (const std::exception& error) {
        emit commandFailed(QString::fromUtf8(error.what()));
        return false;
    }
}

bool CommandDispatcher::setActiveObject(ObjectId id) {
    if (!scene_->setActiveObject(id)) return false;
    emit sceneChanged();
    return true;
}

bool CommandDispatcher::setObjectVisible(ObjectId id, bool visible) {
    if (!scene_->setObjectVisible(id, visible)) return false;
    emit sceneChanged();
    return true;
}

bool CommandDispatcher::setObjectOpacity(ObjectId id, double opacity) {
    if (!scene_->setObjectOpacity(id, opacity)) return false;
    emit sceneChanged();
    return true;
}

void CommandDispatcher::setRepresentation(Representation representation) {
    scene_->setRepresentation(representation);
    emit sceneChanged();
}

void CommandDispatcher::setColorScheme(ColorScheme scheme) {
    scene_->setColorScheme(scheme);
    emit sceneChanged();
}

void CommandDispatcher::selectAtom(std::optional<std::uint64_t> atomId) {
    scene_->selectAtom(atomId);
    emit sceneChanged();
}

void CommandDispatcher::selectAtom(ObjectId objectId, std::optional<std::uint64_t> atomId) {
    scene_->selectAtom(objectId, atomId);
    emit sceneChanged();
}

void CommandDispatcher::selectAtom(ObjectId objectId, std::uint64_t atomId, bool extend) {
    if (extend) scene_->extendAtomSelection(objectId, atomId);
    else scene_->selectAtom(objectId, atomId);
    emit sceneChanged();
}

void CommandDispatcher::selectAtoms(ObjectId objectId, std::vector<std::uint64_t> atomIds) {
    scene_->selectAtoms(objectId, std::move(atomIds));
    emit sceneChanged();
}

} // namespace jlmol

