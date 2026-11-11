#pragma once

#include "core/MolecularScene.h"

#include <QFutureWatcher>
#include <QObject>
#include <QString>
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
    void requestTrajectoryFrame(ObjectId id, std::size_t index);
    void cancelTrajectoryFrameRequest(ObjectId id);
    [[nodiscard]] std::optional<std::size_t> requestedTrajectoryFrame(const MolecularObject& object) const;
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
    void trajectoryFrameRequestChanged();
    void trajectoryFrameRequestFailed();
    void commandFailed(const QString& message);

private:
    struct FrameRequest {
        MolecularObject original;
        std::size_t index;
    };
    struct FrameResult {
        std::optional<PreparedTrajectoryFrame> frame;
        QString error;
    };

    void startFrameRead();
    void finishFrameRead();

    std::shared_ptr<MolecularScene> scene_;
    std::shared_ptr<const FrameRequest> requestedFrame_;
    std::shared_ptr<const FrameRequest> readingFrame_;
    QFutureWatcher<FrameResult> frameWatcher_;
};

} // namespace jlmol

