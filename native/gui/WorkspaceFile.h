#pragma once

#include "gui/MolecularViewport.h"
#include "gui/TrajectoryAnalysisPanel.h"

#include <QByteArray>
#include <QString>

namespace jlmol {

struct WorkspaceContactMap {
    double cutoff;
    bool cAlphaOnly;
    std::size_t frame;
};

struct WorkspaceSettings {
    static constexpr int layoutVersion = 1;
    MolecularViewport::CameraState camera;
    QByteArray layout;
    TrajectoryAnalysisPanel::Settings trajectoryAnalysis;
    int playbackRate;
    std::optional<WorkspaceContactMap> contactMap;
    std::optional<std::size_t> alignmentReference;
};

void writeWorkspace(const QString& path, const MolecularScene& scene, const WorkspaceSettings& settings);
[[nodiscard]] WorkspaceSettings readWorkspace(const QString& path, MolecularScene& staged);

} // namespace jlmol
