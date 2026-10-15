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
    void setRepresentation(Representation representation);
    void setColorScheme(ColorScheme scheme);
    void selectAtom(std::optional<std::uint64_t> atomId);

signals:
    void sceneChanged();
    void commandFailed(const QString& message);

private:
    std::shared_ptr<MolecularScene> scene_;
};

} // namespace jlmol

