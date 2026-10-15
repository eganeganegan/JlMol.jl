#include "commands/CommandDispatcher.h"

#include <QString>
#include <exception>

namespace jlmol {

CommandDispatcher::CommandDispatcher(std::shared_ptr<MolecularScene> scene, QObject* parent)
    : QObject(parent), scene_(std::move(scene)) {}

bool CommandDispatcher::loadStructure(const std::filesystem::path& path) {
    try {
        auto molecule = std::make_shared<Molecule>(Molecule::loadPdb(path));
        scene_->setMolecule(std::move(molecule));
        emit sceneChanged();
        return true;
    } catch (const std::exception& error) {
        emit commandFailed(QString::fromUtf8(error.what()));
        return false;
    }
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

} // namespace jlmol

