#pragma once

#include "commands/CommandDispatcher.h"
#include "core/MolecularScene.h"
#include "scripting/JuliaRuntime.h"

#include <QMainWindow>
#include <memory>

class QLabel;
class QDockWidget;
class QScrollArea;
class QTreeWidget;
class QTreeWidgetItem;

namespace jlmol {

class JuliaConsole;
class MolecularViewport;
class ScriptEditor;
class TrajectoryPanel;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openStructure(const QString& path);
    bool attachTrajectory(const QString& path);
    bool runIntegrationSelfTest(QString* report = nullptr);
    bool runGraphicsSelfTest(QString* report = nullptr);
    bool runTrajectorySelfTest(QString* report = nullptr);
    bool runScriptEditorSelfTest(QString* report = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void chooseStructure();
    void chooseTrajectory();
    void refreshSceneUi();
    void refreshFrameUi();
    void showError(const QString& message);
    void updateInspector();

private:
    void createMenusAndToolbar();
    void createDocks();
    void restoreSettings();
    void saveSettings();
    void chooseObjectOpacity(ObjectId id);
    bool showAlignment(ObjectId referenceId);
    bool showContactMap(double cutoff, bool cAlphaOnly);
    static void bridgeSceneChanged(void* userData);

    std::shared_ptr<MolecularScene> scene_;
    CommandDispatcher* dispatcher_{};
    MolecularViewport* viewport_{};
    QTreeWidget* objectTree_{};
    QLabel* inspector_{};
    QLabel* analysisSummary_{};
    QLabel* analysisMap_{};
    QScrollArea* analysisMapScroll_{};
    QDockWidget* analysisDock_{};
    QDockWidget* trajectoryDock_{};
    TrajectoryPanel* trajectoryPanel_{};
    QLabel* atomCountLabel_{};
    JuliaRuntime juliaRuntime_;
    JuliaConsole* juliaConsole_{};
    ScriptEditor* scriptEditor_{};
};

} // namespace jlmol

