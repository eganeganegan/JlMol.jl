#include "gui/MainWindow.h"

#include "bridge/JlMolAPI.h"
#include "gui/JuliaConsole.h"
#include "gui/MolecularViewport.h"

#include <QActionGroup>
#include <QCloseEvent>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMetaObject>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace jlmol {
namespace {

QDockWidget* makeDock(QMainWindow* window, const QString& title, const QString& objectName, QWidget* contents,
                      Qt::DockWidgetArea area) {
    auto* dock = new QDockWidget(title, window);
    dock->setObjectName(objectName);
    dock->setWidget(contents);
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    window->addDockWidget(area, dock);
    return dock;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), scene_(std::make_shared<MolecularScene>()), dispatcher_(new CommandDispatcher(scene_, this)) {
    setObjectName(QStringLiteral("JlMolMainWindow"));
    setWindowTitle(QStringLiteral("JlMol — Julia-Powered Molecular Visualization Workstation"));
    resize(1380, 900);
    setAcceptDrops(true);

    viewport_ = new MolecularViewport(scene_, this);
    setCentralWidget(viewport_);
    createDocks();
    createMenusAndToolbar();

    atomCountLabel_ = new QLabel(QStringLiteral("No structure loaded"), this);
    statusBar()->addPermanentWidget(atomCountLabel_);
    statusBar()->showMessage(QStringLiteral("Ready"));

    connect(dispatcher_, &CommandDispatcher::sceneChanged, viewport_, &MolecularViewport::sceneChanged);
    connect(dispatcher_, &CommandDispatcher::sceneChanged, this, &MainWindow::refreshSceneUi);
    connect(dispatcher_, &CommandDispatcher::commandFailed, this, &MainWindow::showError);
    connect(viewport_, &MolecularViewport::atomPicked, this, [this](std::uint64_t id) { dispatcher_->selectAtom(id); });

    jlmol_set_active_scene(scene_.get());
    jlmol_set_scene_changed_callback(&MainWindow::bridgeSceneChanged, this);
    QString juliaError;
    if (!juliaRuntime_.initialize(&juliaError)) {
        statusBar()->showMessage(juliaError, 8000);
    }
    juliaConsole_ = new JuliaConsole(&juliaRuntime_, this);
    makeDock(this, QStringLiteral("Julia Console"), QStringLiteral("JuliaConsoleDock"), juliaConsole_, Qt::BottomDockWidgetArea);
    restoreSettings();
}

MainWindow::~MainWindow() {
    jlmol_set_scene_changed_callback(nullptr, nullptr);
    jlmol_set_active_scene(nullptr);
}

void MainWindow::createDocks() {
    objectTree_ = new QTreeWidget(this);
    objectTree_->setHeaderLabels({QStringLiteral("Molecular objects")});
    objectTree_->setAlternatingRowColors(true);
    makeDock(this, QStringLiteral("Object Explorer"), QStringLiteral("ObjectExplorerDock"), objectTree_, Qt::LeftDockWidgetArea);

    inspector_ = new QLabel(QStringLiteral("Select an atom in the viewport to inspect it."), this);
    inspector_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    inspector_->setMargin(12);
    inspector_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    makeDock(this, QStringLiteral("Property Inspector"), QStringLiteral("PropertyInspectorDock"), inspector_, Qt::RightDockWidgetArea);

    auto* analysis = new QWidget(this);
    auto* analysisLayout = new QVBoxLayout(analysis);
    auto* analysisLabel = new QLabel(QStringLiteral("Analysis tools become available as validated scientific methods are added."), analysis);
    analysisLabel->setWordWrap(true);
    analysisLayout->addWidget(analysisLabel);
    analysisLayout->addStretch();
    auto* analysisDock = makeDock(this, QStringLiteral("Analysis Workspace"), QStringLiteral("AnalysisDock"), analysis, Qt::BottomDockWidgetArea);
    analysisDock->hide();
}

void MainWindow::createMenusAndToolbar() {
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    auto* open = fileMenu->addAction(QStringLiteral("&Open Structure…"), QKeySequence::Open, this, &MainWindow::chooseStructure);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    auto* projection = viewMenu->addMenu(QStringLiteral("Projection"));
    auto* perspective = projection->addAction(QStringLiteral("Perspective"));
    auto* orthographic = projection->addAction(QStringLiteral("Orthographic"));
    perspective->setCheckable(true);
    orthographic->setCheckable(true);
    auto* projectionGroup = new QActionGroup(this);
    projectionGroup->addAction(perspective);
    projectionGroup->addAction(orthographic);
    perspective->setChecked(true);
    connect(perspective, &QAction::triggered, this, [this] { viewport_->setOrthographic(false); });
    connect(orthographic, &QAction::triggered, this, [this] { viewport_->setOrthographic(true); });
    viewMenu->addAction(QStringLiteral("Fit to Structure"), QKeySequence(Qt::Key_F), viewport_, &MolecularViewport::fitToScene);

    auto* representationMenu = menuBar()->addMenu(QStringLiteral("&Representation"));
    auto addRepresentation = [this, representationMenu](const QString& label, Representation representation) {
        representationMenu->addAction(label, this, [this, representation] { dispatcher_->setRepresentation(representation); });
    };
    addRepresentation(QStringLiteral("Ball and Stick"), Representation::BallAndStick);
    addRepresentation(QStringLiteral("Space Filling"), Representation::SpaceFilling);
    addRepresentation(QStringLiteral("Points"), Representation::Points);
    addRepresentation(QStringLiteral("Lines"), Representation::Lines);

    auto* colorMenu = menuBar()->addMenu(QStringLiteral("&Color"));
    colorMenu->addAction(QStringLiteral("By Element"), this, [this] { dispatcher_->setColorScheme(ColorScheme::Element); });
    colorMenu->addAction(QStringLiteral("By Chain"), this, [this] { dispatcher_->setColorScheme(ColorScheme::Chain); });
    colorMenu->addAction(QStringLiteral("By Residue"), this, [this] { dispatcher_->setColorScheme(ColorScheme::Residue); });

    auto* toolbar = addToolBar(QStringLiteral("Main"));
    toolbar->setObjectName(QStringLiteral("MainToolbar"));
    toolbar->addAction(open);
    toolbar->addAction(QStringLiteral("Fit"), viewport_, &MolecularViewport::fitToScene);
}

bool MainWindow::openStructure(const QString& path) {
    if (!path.endsWith(QStringLiteral(".pdb"), Qt::CaseInsensitive)) {
        showError(QStringLiteral("This milestone currently supports PDB structures. mmCIF and trajectory formats remain disabled."));
        return false;
    }
    if (!dispatcher_->loadStructure(path.toStdString())) return false;
    viewport_->fitToScene();
    statusBar()->showMessage(QStringLiteral("Loaded %1").arg(QFileInfo(path).fileName()), 5000);
    QSettings settings;
    QStringList recent = settings.value(QStringLiteral("recentFiles")).toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    settings.setValue(QStringLiteral("recentFiles"), recent.mid(0, 10));
    return true;
}

bool MainWindow::runIntegrationSelfTest(QString* report) {
    const auto molecule = scene_->molecule();
    if (!molecule || molecule->atoms().empty()) {
        if (report) *report = QStringLiteral("No structure is loaded");
        return false;
    }
    dispatcher_->selectAtom(molecule->atoms().front().id);
    const QString assignment = juliaRuntime_.evaluate(QStringLiteral("persistent_probe = 41"));
    const QString persistence = juliaRuntime_.evaluate(QStringLiteral("persistent_probe + 1"));
    const QString failure = juliaRuntime_.evaluate(QStringLiteral("error(\"intentional integration error\")"));
    const QString output = juliaRuntime_.evaluate(QStringLiteral(
        "using JlMol\n"
        "mol = active_structure()\n"
        "count = atom_count(mol)\n"
        "setrepresentation!(mol, :spacefill)\n"
        "colorby!(mol, :chain)\n"
        "(count=count, representation=representation(mol), selected=selected_atom())"));
    const bool passed = juliaRuntime_.isAvailable() &&
                        assignment.contains(QStringLiteral("41")) &&
                        persistence.contains(QStringLiteral("42")) &&
                        failure.contains(QStringLiteral("intentional integration error")) &&
                        scene_->representation() == Representation::SpaceFilling &&
                        scene_->colorScheme() == ColorScheme::Chain &&
                        output.contains(QStringLiteral("selected")) &&
                        output.contains(QString::number(molecule->atoms().size()));
    if (report) {
        *report = QStringLiteral("persistent result: %1\nerror path: %2\nbridge result: %3")
                      .arg(persistence, failure, output);
    }
    return passed;
}

void MainWindow::chooseStructure() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open molecular structure"), {},
                                                      QStringLiteral("Protein Data Bank (*.pdb);;All files (*)"));
    if (!path.isEmpty()) openStructure(path);
}

void MainWindow::refreshSceneUi() {
    objectTree_->clear();
    const auto molecule = scene_->molecule();
    if (!molecule) {
        atomCountLabel_->setText(QStringLiteral("No structure loaded"));
        updateInspector();
        return;
    }
    auto* root = new QTreeWidgetItem(objectTree_, {QString::fromStdString(molecule->name())});
    root->setExpanded(true);
    std::map<std::string, std::map<std::pair<std::string, int>, int>> hierarchy;
    for (const auto& atom : molecule->atoms()) hierarchy[atom.chainId][{atom.residueName, atom.residueId}]++;
    for (const auto& [chain, residues] : hierarchy) {
        auto* chainItem = new QTreeWidgetItem(root, {QStringLiteral("Chain %1").arg(QString::fromStdString(chain.empty() ? "(blank)" : chain))});
        for (const auto& [residue, count] : residues) {
            new QTreeWidgetItem(chainItem, {QStringLiteral("%1 %2  (%3 atoms)")
                                               .arg(QString::fromStdString(residue.first))
                                               .arg(residue.second)
                                               .arg(count)});
        }
    }
    atomCountLabel_->setText(QStringLiteral("%1 atoms · %2 bonds · %3 · color: %4")
                                 .arg(molecule->atoms().size())
                                 .arg(molecule->bonds().size())
                                 .arg(QString::fromStdString(toString(scene_->representation())))
                                 .arg(QString::fromStdString(toString(scene_->colorScheme()))));
    updateInspector();
}

void MainWindow::updateInspector() {
    const auto atom = scene_->selectedAtom();
    if (!atom) {
        inspector_->setText(QStringLiteral("Select an atom in the viewport to inspect it."));
        return;
    }
    inspector_->setText(QStringLiteral("<h3>%1</h3><table>"
                                       "<tr><td>Element</td><td><b>%2</b></td></tr>"
                                       "<tr><td>Serial</td><td>%3</td></tr>"
                                       "<tr><td>Residue</td><td>%4 %5</td></tr>"
                                       "<tr><td>Chain</td><td>%6</td></tr>"
                                       "<tr><td>Coordinates</td><td>%7, %8, %9 Å</td></tr>"
                                       "<tr><td>Occupancy</td><td>%10</td></tr>"
                                       "<tr><td>B-factor</td><td>%11 Å²</td></tr></table>")
                            .arg(QString::fromStdString(atom->name))
                            .arg(QString::fromStdString(atom->element))
                            .arg(atom->serial)
                            .arg(QString::fromStdString(atom->residueName))
                            .arg(atom->residueId)
                            .arg(QString::fromStdString(atom->chainId))
                            .arg(atom->position.x, 0, 'f', 3)
                            .arg(atom->position.y, 0, 'f', 3)
                            .arg(atom->position.z, 0, 'f', 3)
                            .arg(atom->occupancy, 0, 'f', 2)
                            .arg(atom->bFactor, 0, 'f', 2));
}

void MainWindow::showError(const QString& message) {
    statusBar()->showMessage(message, 10000);
    QMessageBox::critical(this, QStringLiteral("JlMol"), message);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() && event->mimeData()->urls().size() == 1) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (urls.size() == 1 && urls.first().isLocalFile() && openStructure(urls.first().toLocalFile())) event->acceptProposedAction();
}

void MainWindow::restoreSettings() {
    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("mainWindow/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("mainWindow/state")).toByteArray());
}

void MainWindow::saveSettings() {
    QSettings settings;
    settings.setValue(QStringLiteral("mainWindow/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("mainWindow/state"), saveState());
}

void MainWindow::bridgeSceneChanged(void* userData) {
    auto* window = static_cast<MainWindow*>(userData);
    if (!window) return;
    QMetaObject::invokeMethod(window, [window] {
        window->viewport_->sceneChanged();
        window->refreshSceneUi();
    }, Qt::QueuedConnection);
}

} // namespace jlmol

