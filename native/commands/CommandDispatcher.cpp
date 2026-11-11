#include "commands/CommandDispatcher.h"

#include <QPromise>
#include <QThreadPool>
#include <exception>
#include <utility>

namespace jlmol {

CommandDispatcher::CommandDispatcher(std::shared_ptr<MolecularScene> scene, QObject* parent)
    : QObject(parent), scene_(std::move(scene)) {
    connect(&frameWatcher_, &QFutureWatcher<FrameResult>::finished, this, &CommandDispatcher::finishFrameRead);
}

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
    cancelTrajectoryFrameRequest(id);
    try {
        scene_->setTrajectoryFrame(id, index);
        emit frameChanged();
        return true;
    } catch (const std::exception& error) {
        emit commandFailed(QString::fromUtf8(error.what()));
        return false;
    }
}

void CommandDispatcher::requestTrajectoryFrame(ObjectId id, std::size_t index) {
    if (scene_->activeObjectId() != id) return;
    auto object = scene_->object(id);
    if (!object || !object->trajectory) return;
    if (index >= object->trajectory->frameCount()) {
        emit trajectoryFrameRequestFailed();
        emit commandFailed(QStringLiteral("Trajectory frame is out of range"));
        return;
    }
    if (index == object->frameIndex) {
        cancelTrajectoryFrameRequest(id);
        return;
    }
    if (requestedTrajectoryFrame(*object) == index) return;
    requestedFrame_ = std::make_shared<FrameRequest>(FrameRequest{std::move(*object), index});
    startFrameRead();
    emit trajectoryFrameRequestChanged();
}

void CommandDispatcher::cancelTrajectoryFrameRequest(ObjectId id) {
    if (!requestedFrame_ || requestedFrame_->original.id != id) return;
    requestedFrame_.reset();
    emit trajectoryFrameRequestChanged();
}

std::optional<std::size_t> CommandDispatcher::requestedTrajectoryFrame(const MolecularObject& object) const {
    if (!requestedFrame_) return std::nullopt;
    const auto& original = requestedFrame_->original;
    if (original.id != object.id || original.trajectory != object.trajectory || original.molecule != object.molecule) {
        return std::nullopt;
    }
    return requestedFrame_->index;
}

void CommandDispatcher::startFrameRead() {
    if (readingFrame_ || !requestedFrame_) return;
    readingFrame_ = requestedFrame_;
    auto promise = std::make_shared<QPromise<FrameResult>>();
    promise->start();
    frameWatcher_.setFuture(promise->future());
    QThreadPool::globalInstance()->start([request = readingFrame_, promise = std::move(promise)] {
        FrameResult result;
        try {
            result.frame = MolecularScene::prepareTrajectoryFrame(request->original, request->index);
        } catch (const std::exception& error) {
            result.error = QString::fromUtf8(error.what());
        }
        promise->addResult(std::move(result));
        promise->finish();
    });
}

void CommandDispatcher::finishFrameRead() {
    const auto completed = std::exchange(readingFrame_, nullptr);
    if (completed && completed == requestedFrame_) {
        auto result = frameWatcher_.result();
        requestedFrame_.reset();
        const auto& original = completed->original;
        const auto current = scene_->object(original.id);
        const bool currentRequest = scene_->activeObjectId() == original.id && current &&
            current->trajectory == original.trajectory && current->molecule == original.molecule;
        if (currentRequest && result.frame && scene_->applyTrajectoryFrame(original, completed->index, std::move(*result.frame))) {
            emit frameChanged();
        } else {
            emit trajectoryFrameRequestChanged();
            if (currentRequest && !result.error.isEmpty()) {
                emit trajectoryFrameRequestFailed();
                emit commandFailed(result.error);
            }
        }
    }
    startFrameRead();
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

