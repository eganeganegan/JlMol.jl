#pragma once

#include "commands/CommandDispatcher.h"
#include "core/MolecularScene.h"
#include "scripting/JuliaRuntime.h"

#include <QMainWindow>
#include <memory>

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

namespace jlmol {

class JuliaConsole;
class MolecularViewport;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openStructure(const QString& path);
    bool runIntegrationSelfTest(QString* report = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void chooseStructure();
    void refreshSceneUi();
    void showError(const QString& message);
    void updateInspector();

private:
    void createMenusAndToolbar();
    void createDocks();
    void restoreSettings();
    void saveSettings();
    static void bridgeSceneChanged(void* userData);

    std::shared_ptr<MolecularScene> scene_;
    CommandDispatcher* dispatcher_{};
    MolecularViewport* viewport_{};
    QTreeWidget* objectTree_{};
    QLabel* inspector_{};
    QLabel* atomCountLabel_{};
    JuliaRuntime juliaRuntime_;
    JuliaConsole* juliaConsole_{};
};

} // namespace jlmol

