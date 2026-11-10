#pragma once

#include "commands/CommandDispatcher.h"
#include "core/MolecularScene.h"
#include "scripting/JuliaRuntime.h"

#include <QMainWindow>
#include <functional>
#include <memory>

class QLabel;
class QDockWidget;
class QActionGroup;
class QMenu;
class QScrollArea;
class QTreeWidget;
class QTreeWidgetItem;

namespace jlmol {

class JuliaConsole;
class ContactMapWidget;
class MolecularViewport;
class ScriptEditor;
class TrajectoryPanel;
class TrajectoryAnalysisPanel;
struct FigureOptions;

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
    bool runLinkedAnalysisSelfTest(QString* report = nullptr);
    bool runPluginSelfTest(const QString& path, QString* report = nullptr);
    bool runWorkspaceSelfTest(const QString& trajectory, QString* report = nullptr);
    bool runExportSelfTest(const QString& trajectory, QString* report = nullptr);
    bool runPerformanceBenchmark(QString* report);
    bool runScriptEditorSelfTest(QString* report = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void chooseStructure();
    void chooseTrajectory();
    void chooseWorkspaceToOpen();
    void chooseWorkspaceToSave();
    void chooseFigureExport();
    void chooseAnimationExport();
    void refreshSceneUi();
    void refreshFrameUi();
    void showError(const QString& message);
    void updateInspector();

private:
    void createMenusAndToolbar();
    void createPluginMenus(QMenu* analysisMenu, QMenu* colorMenu);
    bool loadPlugin(const QString& path, QString* error);
    void refreshPluginManagement(QMenu* menu);
    void refreshPluginItems(QMenu* menu, const QString& kind);
    void createDocks();
    void restoreSettings();
    void saveSettings();
    bool openWorkspace(const QString& path, QString& message);
    bool saveWorkspace(const QString& path, QString& message);
    bool exportTrajectory(const QString& path, const MolecularObject& object, const FigureOptions& options,
                          int first, int step, int count, int fps, const std::function<bool(int)>& cancelled, QString& error);
    void chooseObjectOpacity(ObjectId id);
    bool showAlignment(ObjectId referenceId, bool reveal = true);
    bool showContactMap(double cutoff, bool cAlphaOnly, std::optional<std::size_t> mapFrame = {}, bool reveal = true);
    void clearContactMap();
    bool refreshContactMap();
    void refreshDefaultAnalysis();
    static void bridgeSceneChanged(void* userData);

    std::shared_ptr<MolecularScene> scene_;
    CommandDispatcher* dispatcher_{};
    MolecularViewport* viewport_{};
    QActionGroup* projectionActions_{};
    QActionGroup* labelActions_{};
    QActionGroup* lightingActions_{};
    QTreeWidget* objectTree_{};
    QLabel* inspector_{};
    QLabel* analysisSummary_{};
    ContactMapWidget* analysisMap_{};
    QScrollArea* analysisMapScroll_{};
    struct ContactMapContext {
        ObjectId object;
        std::shared_ptr<const Trajectory> trajectory;
        std::size_t frame;
        int atomCount;
        int contacts;
        double cutoff;
        bool cAlphaOnly;
    };
    std::optional<ContactMapContext> contactMap_;
    std::optional<ObjectId> alignmentReference_;
    std::weak_ptr<const Molecule> radiusSource_;
    double cachedRadius_{};
    bool defaultAnalysisPending_{};
    QDockWidget* analysisDock_{};
    QDockWidget* trajectoryDock_{};
    TrajectoryPanel* trajectoryPanel_{};
    QDockWidget* trajectoryAnalysisDock_{};
    TrajectoryAnalysisPanel* trajectoryAnalysis_{};
    QLabel* atomCountLabel_{};
    JuliaRuntime juliaRuntime_;
    JuliaConsole* juliaConsole_{};
    ScriptEditor* scriptEditor_{};
};

} // namespace jlmol

