#pragma once

#include "core/MolecularScene.h"

#include <QObject>
#include <filesystem>
#include <memory>

namespace jlmol {

class CommandDispatcher final : public QObject {
    Q_OBJECT
public:
    explicit CommandDispatcher(std::shared_ptr<MolecularScene> scene, QObject* parent = nullptr);

    bool loadStructure(const std::filesystem::path& path);
    bool attachTrajectory(ObjectId id, const std::filesystem::path& path);
    bool setTrajectoryFrame(ObjectId id, std::size_t index);
    bool closeObject(ObjectId id);
    bool setActiveObject(ObjectId id);
    bool setObjectVisible(ObjectId id, bool visible);
    bool setObjectOpacity(ObjectId id, double opacity);
    void setRepresentation(Representation representation);
    void setColorScheme(ColorScheme scheme);
    void selectAtom(std::optional<std::uint64_t> atomId);
    void selectAtom(ObjectId objectId, std::optional<std::uint64_t> atomId);
    void selectAtom(ObjectId objectId, std::uint64_t atomId, bool extend);
    void selectAtoms(ObjectId objectId, std::vector<std::uint64_t> atomIds);

signals:
    void sceneChanged();
    void frameChanged();
    void commandFailed(const QString& message);

private:
    std::shared_ptr<MolecularScene> scene_;
};

} // namespace jlmol

