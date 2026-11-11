#include "gui/MainWindow.h"
#include "app/ApplicationPaths.h"

#include "bridge/JlMolAPI.h"
#include "core/Analysis.h"
#include "gui/ContactMapWidget.h"
#include "gui/FigureExport.h"
#include "gui/JuliaConsole.h"
#include "gui/MolecularViewport.h"
#include "gui/ScriptEditor.h"
#include "gui/TrajectoryPanel.h"
#include "gui/TrajectoryAnalysisPanel.h"
#include "gui/WorkspaceFile.h"

#include <QActionGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImage>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPushButton>
#include <QProgressDialog>
#include <QSettings>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>
#include <limits>
#include <unordered_map>

namespace jlmol {
namespace {

constexpr int objectIdRole = Qt::UserRole;
constexpr int itemKindRole = Qt::UserRole + 1;
constexpr int chainRole = Qt::UserRole + 2;
constexpr int residueNameRole = Qt::UserRole + 3;
constexpr int residueIdRole = Qt::UserRole + 4;
constexpr int objectItemKind = 0;
constexpr int chainItemKind = 1;
constexpr int residueItemKind = 2;

QDockWidget* makeDock(QMainWindow* window, const QString& title, const QString& objectName, QWidget* contents,
                      Qt::DockWidgetArea area) {
    auto* dock = new QDockWidget(title, window);
    dock->setObjectName(objectName);
    dock->setWidget(contents);
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    window->addDockWidget(area, dock);
    return dock;
}

struct ExportRequest {
    FigureOptions figure;
    int first{};
    int stride{1};
    int count{1};
    int fps{};
};

std::optional<ExportRequest> chooseExportOptions(QWidget* parent, QSize viewport, int frames = 0, int playbackRate = 12) {
    QDialog dialog(parent);
    dialog.setWindowTitle(frames ? QStringLiteral("Export Animation") : QStringLiteral("Export Figure"));
    QFormLayout form(&dialog);
    QComboBox format;
    if (!frames) {
        format.addItems({QStringLiteral("PNG"), QStringLiteral("SVG with editable labels")});
        form.addRow(QStringLiteral("Format"), &format);
    }
    QSpinBox width, height;
    width.setRange(1, 8192);
    height.setRange(1, 8192);
    QCheckBox locked(QStringLiteral("Match viewport proportions"));
    locked.setChecked(true);
    const auto aspect = double(std::max(1, viewport.width())) / std::max(1, viewport.height());
    width.setValue(frames ? 1280 : 2400);
    height.setValue(qRound(width.value() / aspect));
    form.addRow(QStringLiteral("Width (px)"), &width);
    form.addRow(QStringLiteral("Height (px)"), &height);
    form.addRow(&locked);
    QCheckBox transparent(QStringLiteral("Transparent background"));
    QCheckBox labels(QStringLiteral("Include viewport labels"));
    labels.setChecked(true);
    form.addRow(&transparent);
    form.addRow(&labels);
    QSpinBox first, last, stride, fps;
    if (frames) {
        first.setRange(1, frames);
        last.setRange(1, frames);
        last.setValue(frames);
        stride.setRange(1, frames);
        stride.setValue((frames - 1) / 200 + 1);
        fps.setRange(1, 60);
        fps.setValue(playbackRate);
        form.addRow(QStringLiteral("First frame"), &first);
        form.addRow(QStringLiteral("Last frame"), &last);
        form.addRow(QStringLiteral("Frame step"), &stride);
        form.addRow(QStringLiteral("Frames per second"), &fps);
    }
    QLabel detail;
    detail.setWordWrap(true);
    form.addRow(&detail);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    const auto refresh = [&] {
        height.setEnabled(!locked.isChecked());
        const int wantedHeight = locked.isChecked() ? qRound(width.value() / aspect) : height.value();
        if (locked.isChecked()) {
            const QSignalBlocker blocked(&height);
            height.setValue(std::max(1, wantedHeight));
        }
        const QSize pixels(width.value(), height.value());
        const int count = frames && first.value() <= last.value() ? (last.value() - first.value()) / stride.value() + 1 : 0;
        bool valid = wantedHeight <= 8192 && MolecularViewport::supportsImageSize(pixels);
        if (!valid) {
            detail.setText(QStringLiteral("Use at most 8,192 pixels per side and 16,777,216 pixels total."));
        } else if (frames && (count < 1 || count > 2000)) {
            valid = false;
            detail.setText(QStringLiteral("Choose a range and step that export 1 to 2,000 frames."));
        } else if (frames) {
            detail.setText(QStringLiteral("%1 frames · %2 s · Animated PNG").arg(count).arg(double(count) / fps.value(), 0, 'f', 2));
        } else if (format.currentIndex() == 1) {
            detail.setText(QStringLiteral("Molecular geometry is embedded as an image. Labels remain editable SVG text."));
        } else {
            detail.setText(QStringLiteral("PNG with 300 DPI print metadata."));
        }
        buttons.button(QDialogButtonBox::Ok)->setEnabled(valid);
    };
    for (auto* field : {&width, &height, &first, &last, &stride, &fps}) {
        QObject::connect(field, &QSpinBox::valueChanged, &dialog, refresh);
    }
    QObject::connect(&locked, &QCheckBox::toggled, &dialog, refresh);
    QObject::connect(&format, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    refresh();
    if (dialog.exec() != QDialog::Accepted) return {};
    return ExportRequest{{{width.value(), height.value()}, transparent.isChecked(), labels.isChecked(), format.currentIndex() == 1},
        frames ? first.value() - 1 : 0, frames ? stride.value() : 1,
        frames ? (last.value() - first.value()) / stride.value() + 1 : 1, frames ? fps.value() : 0};
}

QString chooseExportPath(QWidget* parent, const QString& title, const QString& filter, const QString& suffix) {
    QFileDialog dialog(parent, title, {}, filter);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix(suffix);
    return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles().value(0) : QString{};
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), scene_(std::make_shared<MolecularScene>()), dispatcher_(new CommandDispatcher(scene_, this)) {
    setObjectName(QStringLiteral("JlMolMainWindow"));
    setWindowTitle(QStringLiteral("JlMol: Julia-Powered Molecular Visualization Workstation"));
    resize(1380, 900);
    setAcceptDrops(true);

    viewport_ = new MolecularViewport(scene_, this);
    setCentralWidget(viewport_);
    createDocks();

    atomCountLabel_ = new QLabel(QStringLiteral("No structure loaded"), this);
    statusBar()->addPermanentWidget(atomCountLabel_);
    statusBar()->showMessage(QStringLiteral("Ready"));

    connect(dispatcher_, &CommandDispatcher::sceneChanged, viewport_, &MolecularViewport::sceneChanged);
    connect(dispatcher_, &CommandDispatcher::sceneChanged, this, &MainWindow::refreshSceneUi);
    connect(dispatcher_, &CommandDispatcher::frameChanged, viewport_, &MolecularViewport::sceneChanged);
    connect(dispatcher_, &CommandDispatcher::frameChanged, this, &MainWindow::refreshFrameUi);
    connect(dispatcher_, &CommandDispatcher::commandFailed, this, &MainWindow::showError);
    connect(viewport_, &MolecularViewport::atomPicked, this,
            [this](ObjectId objectId, std::uint64_t atomId, bool extend) { dispatcher_->selectAtom(objectId, atomId, extend); });

    jlmol_set_active_scene(scene_.get());
    jlmol_set_scene_changed_callback(&MainWindow::bridgeSceneChanged, this);
    QString juliaError;
    if (!juliaRuntime_.initialize(&juliaError)) {
        statusBar()->showMessage(juliaError, 8000);
    }
    juliaConsole_ = new JuliaConsole(&juliaRuntime_, this);
    makeDock(this, QStringLiteral("Julia Console"), QStringLiteral("JuliaConsoleDock"), juliaConsole_, Qt::BottomDockWidgetArea);
    scriptEditor_ = new ScriptEditor(&juliaRuntime_, this);
    makeDock(this, QStringLiteral("Julia Script Editor"), QStringLiteral("ScriptEditorDock"), scriptEditor_, Qt::BottomDockWidgetArea);
    createMenusAndToolbar();
    restoreSettings();
}

MainWindow::~MainWindow() {
    jlmol_set_scene_changed_callback(nullptr, nullptr);
    jlmol_set_active_scene(nullptr);
}

void MainWindow::createDocks() {
    trajectoryPanel_ = new TrajectoryPanel(scene_, dispatcher_, this);
    trajectoryDock_ = makeDock(this, QStringLiteral("Trajectory"), QStringLiteral("TrajectoryDock"),
                               trajectoryPanel_, Qt::BottomDockWidgetArea);
    trajectoryDock_->hide();
    objectTree_ = new QTreeWidget(this);
    objectTree_->setHeaderLabels({QStringLiteral("Molecular objects")});
    objectTree_->setAlternatingRowColors(true);
    objectTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(objectTree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        auto* item = objectTree_->currentItem();
        if (!item) return;
        auto* root = item;
        while (root->parent()) root = root->parent();
        const auto objectId = root->data(0, objectIdRole).toULongLong();
        if (item->data(0, itemKindRole).toInt() == objectItemKind) {
            dispatcher_->setActiveObject(objectId);
            return;
        }
        const auto object = scene_->object(objectId);
        if (!object || !object->molecule) return;
        const auto chain = item->data(0, chainRole).toString().toStdString();
        const bool isResidue = item->data(0, itemKindRole).toInt() == residueItemKind;
        const auto residueName = item->data(0, residueNameRole).toString().toStdString();
        const auto residueId = item->data(0, residueIdRole).toInt();
        std::vector<std::uint64_t> atomIds;
        for (const auto& atom : object->molecule->atoms()) {
            if (atom.chainId == chain && (!isResidue || (atom.residueName == residueName && atom.residueId == residueId))) {
                atomIds.push_back(atom.id);
            }
        }
        dispatcher_->selectAtoms(objectId, std::move(atomIds));
    });
    connect(objectTree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (column == 0 && item && !item->parent()) {
            dispatcher_->setObjectVisible(item->data(0, Qt::UserRole).toULongLong(), item->checkState(0) == Qt::Checked);
        }
    });
    connect(objectTree_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        auto* item = objectTree_->itemAt(position);
        if (!item) return;
        while (item->parent()) item = item->parent();
        const auto objectId = item->data(0, objectIdRole).toULongLong();
        const auto object = scene_->object(objectId);
        if (!object) return;
        QMenu menu(objectTree_);
        menu.addAction(object->visible ? QStringLiteral("Hide") : QStringLiteral("Show"), this,
                       [this, objectId, object] { dispatcher_->setObjectVisible(objectId, !object->visible); });
        menu.addAction(QStringLiteral("Set Opacity…"), this, [this, objectId] { chooseObjectOpacity(objectId); });
        auto* representationMenu = menu.addMenu(QStringLiteral("Representation"));
        const std::pair<QString, Representation> representations[] = {
            {QStringLiteral("Ball and Stick"), Representation::BallAndStick},
            {QStringLiteral("Space Filling"), Representation::SpaceFilling},
            {QStringLiteral("Points"), Representation::Points},
            {QStringLiteral("Lines"), Representation::Lines},
            {QStringLiteral("C-alpha Trace"), Representation::CAlphaTrace},
            {QStringLiteral("Backbone"), Representation::Backbone},
            {QStringLiteral("Cartoon"), Representation::Cartoon},
            {QStringLiteral("Ribbon"), Representation::Ribbon}};
        for (const auto& [label, representation] : representations) {
            representationMenu->addAction(label, this, [this, objectId, representation] {
                dispatcher_->setActiveObject(objectId);
                dispatcher_->setRepresentation(representation);
            });
        }
        auto* colorMenu = menu.addMenu(QStringLiteral("Color"));
        const std::pair<QString, ColorScheme> colors[] = {
            {QStringLiteral("By Element"), ColorScheme::Element},
            {QStringLiteral("By Chain"), ColorScheme::Chain},
            {QStringLiteral("By Residue"), ColorScheme::Residue},
            {QStringLiteral("By Secondary Structure"), ColorScheme::SecondaryStructure},
            {QStringLiteral("By Residue Sequence"), ColorScheme::Sequence}};
        for (const auto& [label, scheme] : colors) {
            colorMenu->addAction(label, this, [this, objectId, scheme] {
                dispatcher_->setActiveObject(objectId);
                dispatcher_->setColorScheme(scheme);
            });
        }
        menu.addSeparator();
        menu.addAction(QStringLiteral("Close Structure"), this, [this, objectId] { dispatcher_->closeObject(objectId); });
        menu.exec(objectTree_->viewport()->mapToGlobal(position));
    });
    makeDock(this, QStringLiteral("Object Explorer"), QStringLiteral("ObjectExplorerDock"), objectTree_, Qt::LeftDockWidgetArea);

    inspector_ = new QLabel(QStringLiteral("Select an atom in the viewport to inspect it."), this);
    inspector_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    inspector_->setMargin(12);
    inspector_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    makeDock(this, QStringLiteral("Property Inspector"), QStringLiteral("PropertyInspectorDock"), inspector_, Qt::RightDockWidgetArea);

    auto* analysis = new QWidget(this);
    auto* analysisLayout = new QVBoxLayout(analysis);
    analysisSummary_ = new QLabel(QStringLiteral("Open a structure to calculate geometric properties."), analysis);
    analysisSummary_->setWordWrap(true);
    analysisSummary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    analysisLayout->addWidget(analysisSummary_);
    analysisMap_ = new ContactMapWidget(analysis);
    analysisMapScroll_ = new QScrollArea(analysis);
    analysisMapScroll_->setWidget(analysisMap_);
    analysisMapScroll_->setWidgetResizable(false);
    analysisLayout->addWidget(analysisMapScroll_);
    analysisMapScroll_->hide();
    analysisMapScroll_->setMinimumHeight(160);
    connect(analysisMap_, &ContactMapWidget::pairActivated, this, [this](auto first, auto second) {
        if (!contactMap_ || scene_->activeObjectId() != contactMap_->object) return;
        const auto object = scene_->object(contactMap_->object);
        if (!object || object->trajectory != contactMap_->trajectory) return;
        dispatcher_->selectAtoms(contactMap_->object, first == second ? std::vector<std::uint64_t>{first}
                                                                      : std::vector<std::uint64_t>{first, second});
    });
    analysisLayout->addStretch();
    analysisDock_ = makeDock(this, QStringLiteral("Analysis Workspace"), QStringLiteral("AnalysisDock"), analysis, Qt::BottomDockWidgetArea);
    analysisDock_->hide();
    connect(analysisDock_, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible && defaultAnalysisPending_ && !contactMap_ && !alignmentReference_) {
            refreshDefaultAnalysis();
            updateInspector();
        }
    });
    trajectoryAnalysis_ = new TrajectoryAnalysisPanel(scene_, dispatcher_, &juliaRuntime_, this);
    trajectoryAnalysisDock_ = makeDock(this, QStringLiteral("Trajectory Analysis"), QStringLiteral("TrajectoryAnalysisDock"),
                                       trajectoryAnalysis_, Qt::BottomDockWidgetArea);
    trajectoryAnalysisDock_->hide();
}

void MainWindow::chooseObjectOpacity(ObjectId id) {
    const auto value = scene_->objectOpacity(id);
    if (!value) return;
    bool accepted = false;
    const auto percentage = QInputDialog::getDouble(this, QStringLiteral("Structure opacity"),
        QStringLiteral("Opacity (%)"), *value * 100.0, 0.0, 100.0, 1, &accepted);
    if (accepted) dispatcher_->setObjectOpacity(id, percentage / 100.0);
}

void MainWindow::createMenusAndToolbar() {
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    auto* open = fileMenu->addAction(QStringLiteral("&Open Structure…"), QKeySequence::Open, this, &MainWindow::chooseStructure);
    fileMenu->addAction(QStringLiteral("Attach &Trajectory…"), this, &MainWindow::chooseTrajectory);
    fileMenu->addAction(QStringLiteral("Open Workspace…"), this, &MainWindow::chooseWorkspaceToOpen);
    fileMenu->addAction(QStringLiteral("Save Workspace…"), this, &MainWindow::chooseWorkspaceToSave);
    fileMenu->addAction(QStringLiteral("Export Figure…"), this, &MainWindow::chooseFigureExport);
    fileMenu->addAction(QStringLiteral("Export Animation…"), this, &MainWindow::chooseAnimationExport);
    fileMenu->addAction(QStringLiteral("&Close Active Structure"), QKeySequence::Close, this, [this] {
        const auto active = scene_->activeObjectId();
        if (active) dispatcher_->closeObject(*active);
    });
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(trajectoryDock_->toggleViewAction());
    auto* projection = viewMenu->addMenu(QStringLiteral("Projection"));
    auto* perspective = projection->addAction(QStringLiteral("Perspective"));
    auto* orthographic = projection->addAction(QStringLiteral("Orthographic"));
    perspective->setCheckable(true);
    orthographic->setCheckable(true);
    projectionActions_ = new QActionGroup(this);
    perspective->setData(false);
    orthographic->setData(true);
    projectionActions_->addAction(perspective);
    projectionActions_->addAction(orthographic);
    perspective->setChecked(true);
    connect(perspective, &QAction::triggered, this, [this] { viewport_->setOrthographic(false); });
    connect(orthographic, &QAction::triggered, this, [this] { viewport_->setOrthographic(true); });
    viewMenu->addAction(QStringLiteral("Fit to Structure"), QKeySequence(Qt::Key_F), viewport_, &MolecularViewport::fitToScene);
    auto* presets = viewMenu->addMenu(QStringLiteral("Camera Presets"));
    const std::pair<QString, ViewPreset> views[] = {
        {QStringLiteral("Front"), ViewPreset::Front}, {QStringLiteral("Back"), ViewPreset::Back},
        {QStringLiteral("Left"), ViewPreset::Left}, {QStringLiteral("Right"), ViewPreset::Right},
        {QStringLiteral("Top"), ViewPreset::Top}, {QStringLiteral("Bottom"), ViewPreset::Bottom}};
    for (const auto& [label, preset] : views) {
        presets->addAction(label, this, [this, preset] { viewport_->setViewPreset(preset); });
    }
    auto* labels = viewMenu->addMenu(QStringLiteral("Labels"));
    labelActions_ = new QActionGroup(this);
    const std::pair<QString, LabelMode> labelModes[] = {
        {QStringLiteral("None"), LabelMode::None},
        {QStringLiteral("Selected Atoms"), LabelMode::SelectedAtoms},
        {QStringLiteral("Selected Residues"), LabelMode::SelectedResidues}};
    for (const auto& [label, mode] : labelModes) {
        auto* action = labels->addAction(label);
        action->setCheckable(true);
        action->setData(static_cast<int>(mode));
        labelActions_->addAction(action);
        if (mode == LabelMode::None) action->setChecked(true);
        connect(action, &QAction::triggered, this, [this, mode] { viewport_->setLabelMode(mode); });
    }
    auto* background = viewMenu->addMenu(QStringLiteral("Background"));
    background->addAction(QStringLiteral("Dark"), this, [this] { viewport_->setBackground(QColor(9, 12, 17)); });
    background->addAction(QStringLiteral("White"), this, [this] { viewport_->setBackground(Qt::white); });
    background->addAction(QStringLiteral("Custom Color…"), this, [this] {
        const auto color = QColorDialog::getColor(viewport_->cameraState().background, this, QStringLiteral("Background Color"));
        if (color.isValid()) viewport_->setBackground(color);
    });
    auto* lighting = viewMenu->addMenu(QStringLiteral("Lighting"));
    lightingActions_ = new QActionGroup(this);
    const std::pair<QString, LightingPreset> lightingPresets[] = {
        {QStringLiteral("Standard"), LightingPreset::Standard}, {QStringLiteral("Soft"), LightingPreset::Soft},
        {QStringLiteral("Flat"), LightingPreset::Flat}};
    for (const auto& [label, preset] : lightingPresets) {
        auto* action = lighting->addAction(label, this, [this, preset] { viewport_->setLighting(preset); });
        action->setCheckable(true);
        action->setData(static_cast<int>(preset));
        lightingActions_->addAction(action);
        if (preset == LightingPreset::Standard) action->setChecked(true);
    }

    auto* representationMenu = menuBar()->addMenu(QStringLiteral("&Representation"));
    representationMenu->addAction(QStringLiteral("Set Active Structure Opacity…"), this, [this] {
        if (const auto active = scene_->activeObjectId()) chooseObjectOpacity(*active);
    });
    representationMenu->addSeparator();
    auto addRepresentation = [this, representationMenu](const QString& label, Representation representation) {
        representationMenu->addAction(label, this, [this, representation] { dispatcher_->setRepresentation(representation); });
    };
    addRepresentation(QStringLiteral("Ball and Stick"), Representation::BallAndStick);
    addRepresentation(QStringLiteral("Space Filling"), Representation::SpaceFilling);
    addRepresentation(QStringLiteral("Points"), Representation::Points);
    addRepresentation(QStringLiteral("Lines"), Representation::Lines);
    representationMenu->addSeparator();
    addRepresentation(QStringLiteral("C-alpha Trace"), Representation::CAlphaTrace);
    addRepresentation(QStringLiteral("Backbone"), Representation::Backbone);
    addRepresentation(QStringLiteral("Cartoon"), Representation::Cartoon);
    addRepresentation(QStringLiteral("Ribbon"), Representation::Ribbon);

    auto* colorMenu = menuBar()->addMenu(QStringLiteral("&Color"));
    colorMenu->addAction(QStringLiteral("By Element"), this, [this] { dispatcher_->setColorScheme(ColorScheme::Element); });
    colorMenu->addAction(QStringLiteral("By Chain"), this, [this] { dispatcher_->setColorScheme(ColorScheme::Chain); });
    colorMenu->addAction(QStringLiteral("By Residue"), this, [this] { dispatcher_->setColorScheme(ColorScheme::Residue); });
    colorMenu->addAction(QStringLiteral("By Secondary Structure"), this,
                         [this] { dispatcher_->setColorScheme(ColorScheme::SecondaryStructure); });
    colorMenu->addAction(QStringLiteral("By Residue Sequence"), this, [this] { dispatcher_->setColorScheme(ColorScheme::Sequence); });

    auto* analysisMenu = menuBar()->addMenu(QStringLiteral("&Analysis"));
    analysisMenu->addAction(analysisDock_->toggleViewAction());
    analysisMenu->addAction(trajectoryAnalysisDock_->toggleViewAction());
    analysisMenu->addAction(QStringLiteral("Show Radius of Gyration"), this, [this] {
        const auto molecule = scene_->molecule();
        if (!molecule) {
            showError(QStringLiteral("Open a molecular structure before running analysis."));
            return;
        }
        analysisDock_->show();
        analysisDock_->raise();
        analysisSummary_->setText(QStringLiteral("<h3>Radius of gyration</h3><p>%1 Å</p><p>%2 atoms : mass-weighted about the center of mass</p>")
                                      .arg(analysis::radiusOfGyration(*molecule), 0, 'f', 3)
                                      .arg(molecule->atoms().size()));
        clearContactMap();
        alignmentReference_.reset();
    });
    analysisMenu->addAction(QStringLiteral("Align and Compare Structures…"), this, [this] {
        const auto active = scene_->activeObjectId();
        QStringList choices;
        std::vector<ObjectId> ids;
        for (const auto& object : scene_->objects()) {
            if (object.id == active || !object.molecule) continue;
            choices.push_back(QStringLiteral("%1 [object %2]")
                                  .arg(QString::fromStdString(object.molecule->name())).arg(object.id));
            ids.push_back(object.id);
        }
        if (choices.empty()) {
            showError(QStringLiteral("Open two structures to compare them."));
            return;
        }
        bool accepted = false;
        const auto choice = QInputDialog::getItem(this, QStringLiteral("Structural alignment"),
                                                   QStringLiteral("Reference structure:"), choices, 0, false, &accepted);
        if (accepted) showAlignment(ids[choices.indexOf(choice)]);
    });
    analysisMenu->addAction(QStringLiteral("Contact Map…"), this, [this] {
        if (!scene_->molecule()) {
            showError(QStringLiteral("Open a structure before calculating contacts."));
            return;
        }
        bool accepted = false;
        const auto selection = QInputDialog::getItem(this, QStringLiteral("Contact map"),
            QStringLiteral("Atoms on both axes:"), {QStringLiteral("C-alpha atoms"), QStringLiteral("All atoms")},
            0, false, &accepted);
        if (!accepted) return;
        const double cutoff = QInputDialog::getDouble(this, QStringLiteral("Contact map"),
            QStringLiteral("Distance cutoff (Å):"), 8.0, 0.01, 1000.0, 2, &accepted);
        if (accepted) showContactMap(cutoff, selection == QStringLiteral("C-alpha atoms"));
    });

    auto* scriptMenu = menuBar()->addMenu(QStringLiteral("&Script"));
    scriptMenu->addAction(QStringLiteral("New Script"), QKeySequence(QStringLiteral("Ctrl+Shift+N")),
                          scriptEditor_, &ScriptEditor::newDocument);
    scriptMenu->addAction(QStringLiteral("Open Script…"), QKeySequence(QStringLiteral("Ctrl+Shift+O")),
                          scriptEditor_, &ScriptEditor::openDocument);
    scriptMenu->addAction(QStringLiteral("Save Script"), QKeySequence::Save,
                          scriptEditor_, &ScriptEditor::saveDocument);
    scriptMenu->addAction(QStringLiteral("Save Script As…"), QKeySequence::SaveAs,
                          scriptEditor_, &ScriptEditor::saveDocumentAs);
    scriptMenu->addSeparator();
    scriptMenu->addAction(QStringLiteral("Run Selection"), QKeySequence(QStringLiteral("Ctrl+Return")),
                          scriptEditor_, &ScriptEditor::runSelection);
    scriptMenu->addAction(QStringLiteral("Run File"), QKeySequence(QStringLiteral("Ctrl+Shift+Return")),
                          scriptEditor_, &ScriptEditor::runScript);

    createPluginMenus(analysisMenu, colorMenu);

    auto* toolbar = addToolBar(QStringLiteral("Main"));
    toolbar->setObjectName(QStringLiteral("MainToolbar"));
    toolbar->addAction(open);
    toolbar->addAction(QStringLiteral("Fit"), viewport_, &MolecularViewport::fitToScene);
}

void MainWindow::createPluginMenus(QMenu* analysisMenu, QMenu* colorMenu) {
    auto* plugins = menuBar()->addMenu(QStringLiteral("&Plugins"));
    plugins->setEnabled(juliaRuntime_.isAvailable());
    plugins->addAction(QStringLiteral("Load Local Plugin…"), this, [this] {
        const auto folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/plugins");
        const auto start = QDir().mkpath(folder) ? folder : QDir::homePath();
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Load a trusted local Julia plugin"), start,
                                                       QStringLiteral("Julia plugins (*.jl)"));
        if (path.isEmpty()) return;
        QString error;
        if (!loadPlugin(path, &error)) showError(error);
    });
    auto* management = plugins->addMenu(QStringLiteral("Enabled Plugins"));
    management->setObjectName(QStringLiteral("PluginManagementMenu"));
    management->setToolTipsVisible(true);
    management->addAction(QStringLiteral("No plugins loaded"))->setEnabled(false);
    connect(management, &QMenu::aboutToShow, this, [this, management] { refreshPluginManagement(management); });

    const auto addItems = [this](QMenu* parent, const QString& title, const QString& kind) {
        auto* menu = parent->addMenu(title);
        menu->setObjectName(QStringLiteral("PluginItems-%1").arg(kind));
        menu->setEnabled(juliaRuntime_.isAvailable());
        menu->setToolTipsVisible(true);
        menu->addAction(QStringLiteral("No enabled plugin items"))->setEnabled(false);
        connect(menu, &QMenu::aboutToShow, this, [this, menu, kind] { refreshPluginItems(menu, kind); });
    };
    addItems(plugins, QStringLiteral("Commands"), QStringLiteral("command"));
    addItems(analysisMenu, QStringLiteral("Plugins"), QStringLiteral("analysis"));
    addItems(colorMenu, QStringLiteral("Plugins"), QStringLiteral("color"));
}

bool MainWindow::loadPlugin(const QString& path, QString* error) {
    const auto result = juliaRuntime_.pluginRequest(QStringLiteral("load"), path);
    if (!result.error.isEmpty() || result.fields.size() != 2) {
        *error = result.error.isEmpty() ? QStringLiteral("Julia returned invalid plugin metadata.") : result.error;
        return false;
    }
    statusBar()->showMessage(QStringLiteral("Loaded %1 %2").arg(result.fields[0], result.fields[1]), 6000);
    return true;
}

void MainWindow::refreshPluginManagement(QMenu* menu) {
    menu->clear();
    const auto result = juliaRuntime_.pluginRequest(QStringLiteral("plugins"));
    if (result.fields.isEmpty()) {
        auto* empty = menu->addAction(result.error.isEmpty() ? QStringLiteral("No plugins loaded") : result.error);
        empty->setEnabled(false);
        return;
    }
    for (qsizetype i = 0; i + 4 < result.fields.size(); i += 5) {
        const auto id = result.fields[i];
        auto* action = menu->addAction(QStringLiteral("%1 (%2)").arg(result.fields[i + 1], result.fields[i + 2])
                                          .replace(QLatin1String("&"), QLatin1String("&&")));
        action->setObjectName(id);
        action->setToolTip(result.fields[i + 3]);
        action->setCheckable(true);
        action->setChecked(result.fields[i + 4] == QStringLiteral("1"));
        connect(action, &QAction::triggered, this, [this, action, id](bool enabled) {
            const auto updated = juliaRuntime_.pluginRequest(QStringLiteral("enable"), id,
                                                            enabled ? QStringLiteral("1") : QStringLiteral("0"));
            if (updated.error.isEmpty()) return;
            action->setChecked(!enabled);
            showError(updated.error);
        });
    }
}

void MainWindow::refreshPluginItems(QMenu* menu, const QString& kind) {
    menu->clear();
    const auto result = juliaRuntime_.pluginRequest(QStringLiteral("items"), kind);
    if (result.fields.isEmpty()) {
        auto* empty = menu->addAction(result.error.isEmpty() ? QStringLiteral("No enabled plugin items") : result.error);
        empty->setEnabled(false);
        return;
    }
    for (qsizetype i = 0; i + 3 < result.fields.size(); i += 4) {
        const auto id = result.fields[i];
        const auto item = result.fields[i + 1];
        const auto label = result.fields[i + 2];
        auto* action = menu->addAction(QString(label).replace(QLatin1String("&"), QLatin1String("&&")));
        action->setObjectName(id + QLatin1Char('/') + item);
        action->setToolTip(result.fields[i + 3]);
        action->setEnabled(kind == QStringLiteral("command") || scene_->activeObjectId().has_value());
        connect(action, &QAction::triggered, this, [this, id, item, label, kind] {
            const auto output = juliaRuntime_.pluginRequest(QStringLiteral("run"), id, item);
            if (!output.error.isEmpty()) {
                showError(output.error);
            } else if (kind == QStringLiteral("analysis")) {
                clearContactMap();
                alignmentReference_.reset();
                analysisDock_->show();
                analysisDock_->raise();
                analysisSummary_->setText(QStringLiteral("<h3>%1</h3><pre>%2</pre>")
                                              .arg(label.toHtmlEscaped(), output.fields.value(0).toHtmlEscaped()));
            } else {
                const auto message = kind == QStringLiteral("command") ? output.fields.value(0) : QString{};
                statusBar()->showMessage(message.isEmpty() ? label : message, 6000);
            }
        });
    }
}

bool MainWindow::openStructure(const QString& path) {
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

bool MainWindow::attachTrajectory(const QString& path) {
    const auto active = scene_->activeObjectId();
    if (!active) {
        showError(QStringLiteral("Open the matching structure before attaching a trajectory."));
        return false;
    }
    if (!dispatcher_->attachTrajectory(*active, path.toStdString())) return false;
    trajectoryDock_->show();
    trajectoryDock_->raise();
    viewport_->fitToScene();
    return true;
}

bool MainWindow::showAlignment(ObjectId referenceId, bool reveal) {
    const auto active = scene_->activeObjectId();
    if (!active) return false;
    const auto output = juliaRuntime_.evaluate(QStringLiteral(
        "using JlMol\nlet mobile = JlMol.ActiveStructure(UInt64(%1)), reference = JlMol.ActiveStructure(UInt64(%2))\n"
        "fit = JlMol.alignstructures(mobile, reference)\n"
        "raw = JlMol.rmsd(mobile, reference; align=false)\n"
        "println(\"Matched atoms: \", length(fit.mobile_ids))\n"
        "println(\"RMSD before alignment: \", round(raw; digits=6), \" Å\")\n"
        "println(\"RMSD after alignment: \", round(fit.rmsd; digits=6), \" Å\")\n"
        "println(\"Proper rigid rotation and translation fitted; loaded coordinates are unchanged.\")\n"
        "end").arg(*active).arg(referenceId));
    if (reveal) {
        analysisDock_->show();
        analysisDock_->raise();
    }
    clearContactMap();
    analysisSummary_->setText(QStringLiteral("<h3>Structural alignment</h3><pre>%1</pre>").arg(output.toHtmlEscaped()));
    const bool calculated = output.contains(QStringLiteral("RMSD after alignment:"));
    alignmentReference_ = calculated ? std::optional<ObjectId>{referenceId} : std::nullopt;
    return calculated;
}

bool MainWindow::showContactMap(double cutoff, bool cAlphaOnly, std::optional<std::size_t> mapFrame, bool reveal) {
    const auto active = scene_->activeObjectId();
    const auto object = active ? scene_->object(*active) : std::nullopt;
    if (!object) return false;
    const auto frame = mapFrame.value_or(object->frameIndex);
    const auto coordinates = frame == object->frameIndex ? QStringLiteral("data.coordinates") :
        QStringLiteral("JlMol.trajectory_frame(JlMol.ActiveStructure(UInt64(%1)), %2)").arg(*active).arg(frame + 1);
    // A returned Julia string avoids filling the redirected stdout pipe with matrix data.
    const auto output = juliaRuntime_.evaluate(QStringLiteral(
        "using JlMol\nlet data = JlMol.snapshot(JlMol.ActiveStructure(UInt64(%1)))\n"
        "indices = %2\n"
        "isempty(indices) && error(\"No atoms match this contact-map selection\")\n"
        "length(indices) <= 1024 || error(\"GUI contact maps support up to 1024 atoms; choose C-alpha atoms or use the Julia API\")\n"
        "points = %4\n"
        "map = JlMol.contactmap(points[:, indices]; cutoff=%3)\n"
        "join([join(data.atom_ids[indices], \",\"); [join(ifelse.(map[i, :], \"1\", \"0\")) for i in axes(map, 1)]], \"\\n\")\n"
        "end").arg(*active)
        .arg(cAlphaOnly ? QStringLiteral("findall(==(\"CA\"), data.atom_names)")
                       : QStringLiteral("collect(eachindex(data.atom_ids))"))
        .arg(cutoff, 0, 'g', 17).arg(coordinates));
    const auto decoded = QJsonDocument::fromJson((QStringLiteral("[") + output + QStringLiteral("]")).toUtf8());
    const auto rows = decoded.isArray() && decoded.array().size() == 1 && decoded.array()[0].isString()
                          ? decoded.array()[0].toString().split('\n') : QStringList{};
    if (reveal) {
        analysisDock_->show();
        analysisDock_->raise();
    }
    clearContactMap();
    alignmentReference_.reset();
    if (rows.size() < 2) {
        analysisSummary_->setText(QStringLiteral("<h3>Contact map</h3><pre>%1</pre>").arg(output.toHtmlEscaped()));
        return false;
    }
    const auto ids = rows.front().split(',');
    const int count = ids.size();
    if (count > 1024 || rows.size() != count + 1) return false;
    std::vector<std::uint64_t> atomIds;
    atomIds.reserve(count);
    for (const auto& value : ids) {
        bool ok = false;
        const auto id = value.toULongLong(&ok);
        if (!ok || !id) return false;
        atomIds.push_back(id);
    }
    QImage image(count, count, QImage::Format_RGB32);
    int contacts = 0;
    for (int row = 0; row < count; ++row) {
        if (rows[row + 1].size() != count) return false;
        for (int column = 0; column < count; ++column) {
            const auto value = rows[row + 1][column];
            if (value != QLatin1Char('0') && value != QLatin1Char('1')) return false;
            const bool contact = value == QLatin1Char('1');
            image.setPixelColor(column, row, contact ? QColor(44, 162, 170) : QColor(245, 247, 250));
            if (row < column && contact) ++contacts;
        }
    }
    contactMap_ = ContactMapContext{*active, object->trajectory, frame, count, contacts, cutoff, cAlphaOnly};
    analysisMap_->setMatrix(std::move(image), std::move(atomIds));
    return refreshContactMap();
}

void MainWindow::clearContactMap() {
    contactMap_.reset();
    analysisMapScroll_->hide();
    analysisMap_->clear();
}

bool MainWindow::refreshContactMap() {
    if (!contactMap_) return false;
    const auto& map = *contactMap_;
    const auto object = scene_->object(map.object);
    if (!object || scene_->activeObjectId() != map.object || object->trajectory != map.trajectory) {
        clearContactMap();
        return false;
    }
    auto summary = QStringLiteral("<h3>Contact map</h3><p>%1 atoms : %2 unique contacts : cutoff %3 Å</p>")
        .arg(map.atomCount).arg(map.contacts).arg(map.cutoff);
    if (map.trajectory) {
        summary += QStringLiteral("<p>Map frame %1 : viewer frame %2. Reopen Contact Map to recalculate.</p>")
            .arg(map.frame + 1).arg(object->frameIndex + 1);
    }
    summary += QStringLiteral("<p>Teal: contact. Amber: selected atoms. Click a cell to select its atom pair.</p>");
    analysisSummary_->setText(summary);
    const auto selected = scene_->selectedObjectId() == map.object ? scene_->selectedAtomIds() : std::vector<std::uint64_t>{};
    analysisMap_->setSelectedAtoms(selected);
    analysisMapScroll_->show();
    return true;
}

bool MainWindow::runIntegrationSelfTest(QString* report) {
    const auto molecule = scene_->molecule();
    if (!molecule || molecule->atoms().size() < 8) {
        if (report) *report = QStringLiteral("The integration test needs a structure with at least eight atoms");
        return false;
    }
    dispatcher_->selectAtom(molecule->atoms().front().id);
    const QString assignment = juliaRuntime_.evaluate(QStringLiteral("persistent_probe = 41"));
    const QString persistence = juliaRuntime_.evaluate(QStringLiteral("persistent_probe + 1"));
    const QString failure = juliaRuntime_.evaluate(QStringLiteral("error(\"intentional integration error\")"));
    QString sourcePath = QString::fromStdString(molecule->sourcePath().string());
    sourcePath.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    sourcePath.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    const QString output = juliaRuntime_.evaluate(QStringLiteral(
        "using JlMol\n"
        "mol = active_structure()\n"
        "copy = loadstructure(\"%1\")\n"
        "before = length(structures())\n"
        "setvisible!(copy, false)\n"
        "hidden = !isvisible(copy)\n"
        "@assert opacity(mol) == opacity(copy) == 1.0\n"
        "@assert setopacity!(mol, 0.45) == 0.45 && opacity(copy) == 1.0\n"
        "alpha = opacity(mol)\n"
        "setrepresentation!(mol, :cartoon)\n"
        "colorby!(mol, :secondary_structure)\n"
        "d = distance(mol, %2, %3)\n"
        "a = angle(mol, %2, %3, %4)\n"
        "t = dihedral(mol, %2, %3, %4, %5)\n"
        "rg = radius_of_gyration(mol)\n"
        "fit = alignstructures(mol, copy)\n"
        "@assert fit.rmsd < 1e-8\n"
        "@assert rmsd(mol, copy; align=false) == 0\n"
        "@assert length(fit.mobile_ids) == atom_count(mol)\n"
        "contacts = contactmap(mol; cutoff=2.0)\n"
        "@assert size(contacts.contacts) == (atom_count(mol), atom_count(mol))\n"
        "close!(copy)\n"
        "(count=atom_count(mol), before=before, hidden=hidden, after=length(structures()), "
        "representation=representation(mol), alpha=alpha, distance=d, angle=a, dihedral=t, radius=rg, selected=selected_atom())")
        .arg(sourcePath)
        .arg(molecule->atoms()[0].id)
        .arg(molecule->atoms()[1].id)
        .arg(molecule->atoms()[2].id)
        .arg(molecule->atoms()[7].id));
    const auto referenceId = scene_->activeObjectId().value_or(0);
    const auto guiCopyId = scene_->addMolecule(std::make_shared<Molecule>(*molecule));
    const bool alignmentGuiPassed = showAlignment(referenceId);
    const bool contactGuiPassed = showContactMap(2.0, false);
    const bool guiCopyClosed = scene_->closeObject(guiCopyId);
    const bool passed = juliaRuntime_.isAvailable() &&
                        alignmentGuiPassed && contactGuiPassed && guiCopyClosed &&
                        assignment.contains(QStringLiteral("41")) &&
                        persistence.contains(QStringLiteral("42")) &&
                        failure.contains(QStringLiteral("intentional integration error")) &&
                        scene_->representation() == Representation::Cartoon &&
                        scene_->colorScheme() == ColorScheme::SecondaryStructure &&
                        scene_->objectCount() == 1 &&
                        output.contains(QStringLiteral("before = 2")) &&
                        output.contains(QStringLiteral("hidden = true")) &&
                        output.contains(QStringLiteral("after = 1")) &&
                        output.contains(QStringLiteral("alpha = 0.45")) &&
                        scene_->objectOpacity(referenceId) == 0.45 &&
                        output.contains(QStringLiteral("distance")) &&
                        output.contains(QStringLiteral("angle")) &&
                        output.contains(QStringLiteral("dihedral")) &&
                        output.contains(QStringLiteral("radius")) &&
                        output.contains(QStringLiteral("selected")) &&
                        output.contains(QString::number(molecule->atoms().size()));
    if (report) {
        *report = QStringLiteral("persistent result: %1\nerror path: %2\nbridge result: %3\nGUI alignment: %4\nGUI contact map: %5")
                      .arg(persistence, failure, output)
                      .arg(alignmentGuiPassed ? QStringLiteral("passed") : QStringLiteral("failed"))
                      .arg(contactGuiPassed ? QStringLiteral("passed") : QStringLiteral("failed"));
    }
    return passed;
}

bool MainWindow::runGraphicsSelfTest(QString* report) {
    const auto id = scene_->activeObjectId();
    const auto object = id ? scene_->object(*id) : std::nullopt;
    if (!object || !object->molecule || !viewport_->isValid()) {
        if (report) *report = QStringLiteral("The graphics test needs a loaded protein and a native OpenGL window");
        return false;
    }
    const auto grab = [&] { return viewport_->grabFramebuffer().convertToFormat(QImage::Format_RGB32); };
    const auto difference = [](const QImage& a, const QImage& b, int tolerance) {
        if (a.isNull() || a.size() != b.size()) return -1;
        int changed = 0;
        for (int y = 0; y < a.height(); ++y) {
            const auto* left = reinterpret_cast<const QRgb*>(a.constScanLine(y));
            const auto* right = reinterpret_cast<const QRgb*>(b.constScanLine(y));
            for (int x = 0; x < a.width(); ++x) {
                if (qAbs(qRed(left[x]) - qRed(right[x])) + qAbs(qGreen(left[x]) - qGreen(right[x])) +
                    qAbs(qBlue(left[x]) - qBlue(right[x])) > tolerance) ++changed;
            }
        }
        return changed;
    };
    viewport_->setLabelMode(LabelMode::None);
    viewport_->fitToScene();
    (void)scene_->setColorScheme(*id, ColorScheme::SecondaryStructure);
    (void)scene_->setObjectOpacity(*id, 0.0);
    const auto background = grab();
    bool passed = !background.isNull();
    QStringList checks;
    for (const auto rep : {Representation::BallAndStick, Representation::CAlphaTrace,
                           Representation::Cartoon, Representation::Ribbon}) {
        (void)scene_->setRepresentation(*id, rep);
        (void)scene_->setObjectOpacity(*id, 1.0);
        const auto opaque = grab();
        (void)scene_->setObjectOpacity(*id, 0.45);
        const auto translucent = grab();
        const auto painted = difference(background, translucent, 12);
        const auto faded = difference(opaque, translucent, 12);
        const bool result = painted > 40 && faded > 40;
        checks << QStringLiteral("%1: %2 visible blended pixels, %3 changed from opaque")
                      .arg(QString::fromStdString(toString(rep))).arg(painted).arg(faded);
        passed &= result;
    }

    (void)scene_->setRepresentation(*id, Representation::Cartoon);
    (void)scene_->setObjectOpacity(*id, 1.0);
    const auto colored = grab();
    int helixPixels = 0;
    int sheetPixels = 0;
    for (int y = 0; y < colored.height(); ++y) {
        const auto* pixels = reinterpret_cast<const QRgb*>(colored.constScanLine(y));
        for (int x = 0; x < colored.width(); ++x) {
            const auto p = pixels[x];
            if (qRed(p) > 45 && qRed(p) > 2 * qGreen(p) && qRed(p) > 2 * qBlue(p)) ++helixPixels;
            if (qGreen(p) > 45 && qGreen(p) > 2 * qBlue(p) && qRed(p) > qGreen(p)) ++sheetPixels;
        }
    }
    checks << QStringLiteral("Cartoon secondary colors: %1 helix pixels, %2 sheet pixels").arg(helixPixels).arg(sheetPixels);
    passed &= helixPixels > 40 && sheetPixels > 40;

    (void)scene_->setObjectOpacity(*id, 0.0);
    const auto composite = [&](bool reverse) {
        std::array<ObjectId, 2> copies{};
        for (int index = 0; index < 2; ++index) {
            const bool accent = (index == 1) != reverse;
            copies[index] = scene_->addMolecule(object->molecule);
            (void)scene_->setRepresentation(copies[index], Representation::Cartoon);
            (void)scene_->setColorScheme(copies[index], accent ? ColorScheme::Chain : ColorScheme::SecondaryStructure);
            (void)scene_->setObjectOpacity(copies[index], accent ? 0.25 : 0.65);
        }
        const auto image = grab();
        for (const auto copy : copies) (void)scene_->closeObject(copy);
        return image;
    };
    const auto forward = composite(false);
    const auto reversed = composite(true);
    const auto orderDifference = difference(forward, reversed, 6);
    checks << QStringLiteral("Changed pixels after reversing translucent draw order: %1").arg(orderDifference);
    passed &= orderDifference >= 0 && orderDifference < 10 && difference(background, forward, 12) > 40;

    // Identical geometry at the opaque surface should fail the depth test.
    // Both frames use the mixed render path, so their sampling is identical.
    (void)scene_->setObjectOpacity(*id, 1.0);
    const auto overlap = scene_->addMolecule(object->molecule);
    (void)scene_->setRepresentation(overlap, Representation::Cartoon);
    (void)scene_->setColorScheme(overlap, ColorScheme::Chain);
    (void)scene_->setObjectOpacity(overlap, 0.001);
    const auto nearlyHidden = grab();
    (void)scene_->setObjectOpacity(overlap, 0.65);
    const auto occluded = grab();
    const auto leakedPixels = difference(nearlyHidden, occluded, 6);
    checks << QStringLiteral("Changed pixels at the opaque surface: %1").arg(leakedPixels);
    passed &= leakedPixels == 0;
    (void)scene_->closeObject(overlap);

    const auto center = QPointF(viewport_->rect().center());
    const auto finish = center + QPointF(39.25, 23.75);
    QMouseEvent press(QEvent::MouseButtonPress, center, viewport_->mapToGlobal(center.toPoint()),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent drag(QEvent::MouseMove, finish, viewport_->mapToGlobal(finish.toPoint()),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, finish, viewport_->mapToGlobal(finish.toPoint()),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(viewport_, &press);
    QCoreApplication::sendEvent(viewport_, &drag);
    QCoreApplication::sendEvent(viewport_, &release);
    const auto orbited = grab();
    const bool orbitPassed = difference(colored, orbited, 12) > 40;
    int zoomUpdates = 0;
    std::optional<MolecularViewport::CameraState> intermediateCamera;
    {
        QEventLoop zoomEvents;
        connect(viewport_, &MolecularViewport::cameraChanged, &zoomEvents, [&] {
            // Reading the framebuffer here would block the animation on slower runners.
            if (++zoomUpdates == 1) intermediateCamera = viewport_->cameraState();
        });
        QWheelEvent wheel(center, viewport_->mapToGlobal(center.toPoint()), QPoint(0, 155), QPoint(),
                          Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        QCoreApplication::sendEvent(viewport_, &wheel);
        QTimer::singleShot(2000, &zoomEvents, &QEventLoop::quit);
        zoomEvents.exec();
    }
    const auto zoomedCamera = viewport_->cameraState();
    const auto zoomed = grab();
    QImage intermediate;
    if (intermediateCamera) {
        viewport_->restoreCamera(*intermediateCamera);
        intermediate = grab();
        viewport_->restoreCamera(zoomedCamera);
    }
    const bool zoomPassed = zoomUpdates > 2 && difference(orbited, intermediate, 12) > 40 &&
                           difference(intermediate, zoomed, 12) > 40;
    checks << QStringLiteral("Fractional orbit: %1; eased trackpad zoom: %2 (%3 updates)")
        .arg(orbitPassed ? QStringLiteral("passed") : QStringLiteral("failed"),
             zoomPassed ? QStringLiteral("passed") : QStringLiteral("failed")).arg(zoomUpdates);
    passed &= orbitPassed && zoomPassed;
    const auto snapshotPath = qEnvironmentVariable("JLMOL_GRAPHICS_SNAPSHOT");
    if (!snapshotPath.isEmpty()) {
        const bool saved = zoomed.save(snapshotPath);
        checks << QStringLiteral("Cartoon preview: %1").arg(saved ? snapshotPath : QStringLiteral("could not save"));
        passed &= saved;
    }
    (void)scene_->setRepresentation(*id, object->representation);
    (void)scene_->setColorScheme(*id, object->colorScheme);
    (void)scene_->setObjectOpacity(*id, object->opacity);
    if (report) *report = checks.join('\n');
    return passed;
}

bool MainWindow::waitForTrajectoryFrame(ObjectId id, std::size_t index) {
    const auto reached = [&] {
        const auto object = scene_->object(id);
        return object && object->frameIndex == index;
    };
    if (reached()) return true;
    QEventLoop events;
    connect(dispatcher_, &CommandDispatcher::frameChanged, &events, [&] { if (reached()) events.quit(); });
    connect(dispatcher_, &CommandDispatcher::trajectoryFrameRequestFailed, &events, &QEventLoop::quit);
    QTimer::singleShot(5000, &events, &QEventLoop::quit);
    events.exec();
    return reached();
}

bool MainWindow::runTrajectorySelfTest(QString* report) {
    const auto active = scene_->activeObjectId();
    const auto object = active ? scene_->object(*active) : std::nullopt;
    if (!object || !object->trajectory || object->trajectory->frameCount() != 3) {
        if (report) *report = QStringLiteral("Expected the three-frame XYZ fixture");
        return false;
    }
    auto* slider = trajectoryPanel_->findChild<QSlider*>(QStringLiteral("TrajectorySlider"));
    auto* frame = trajectoryPanel_->findChild<QSpinBox*>(QStringLiteral("TrajectoryFrame"));
    auto* play = trajectoryPanel_->findChild<QToolButton*>(QStringLiteral("TrajectoryPlay"));
    const auto atom = object->molecule->atoms().front().id;
    // Attaching the trajectory shows its dock. Settle the layout before taking the baseline image.
    QCoreApplication::processEvents();
    const auto viewportMinimum = viewport_->minimumSize();
    const auto viewportMaximum = viewport_->maximumSize();
    // The event loop can process further dock and window resize events while the seek is in flight.
    viewport_->setFixedSize(viewport_->size());
    const auto unselected = viewport_->grabFramebuffer();
    auto* root = objectTree_->topLevelItem(0);
    root->setExpanded(false);
    dispatcher_->selectAtom(*active, atom);
    const auto selectionUpdate = viewport_->measureRedraw();
    const auto before = viewport_->grabFramebuffer();
    const bool partialSelection = !unselected.isNull() && unselected != before &&
        selectionUpdate.uploadBytes == sizeof(RenderVertex) && objectTree_->topLevelItem(0) == root && !root->isExpanded();
    int committedFrames = 0;
    const auto countFrames = connect(dispatcher_, &CommandDispatcher::frameChanged, this, [&] { ++committedFrames; });
    slider->setValue(2);
    slider->setValue(1);
    slider->setValue(3);
    const bool deferred = scene_->object(*active)->frameIndex == 0;
    const bool reached = waitForTrajectoryFrame(*active, 2);
    disconnect(countFrames);
    const bool coalesced = deferred && reached && committedFrames == 1;
    QCoreApplication::processEvents();
    const auto after = viewport_->grabFramebuffer();
    const auto selected = scene_->selectedAtom();
    const bool seek = frame->value() == 3 && selected && selected->id == atom && selected->position.x == 2.0F;
    const bool rendered = !before.isNull() && before.size() == after.size() && before != after;
    const auto alternate = object->colorScheme == ColorScheme::Element ? ColorScheme::Chain : ColorScheme::Element;
    (void)scene_->setColorScheme(*active, alternate);
    (void)viewport_->grabFramebuffer();
    (void)scene_->setColorScheme(*active, object->colorScheme);
    const bool matchesRebuild = viewport_->grabFramebuffer() == after;
    viewport_->setMinimumSize(viewportMinimum);
    viewport_->setMaximumSize(viewportMaximum);

    const auto script = juliaRuntime_.evaluate(QStringLiteral(
        "using JlMol\n"
        "mol = active_structure()\n"
        "@assert trajectory_info(mol) == (frames=3, frame=3, time_ps=nothing, cell=nothing)\n"
        "@assert trajectory_frame(mol, 1)[1,1] == 0\n"
        "@assert trajectory_info(mol).frame == 3\n"
        "@assert trajectory_rmsd(mol; align=false) ≈ [0, 1, 2]\n"
        "@assert maximum(trajectory_rmsd(mol)) < 1e-6\n"
        "@assert maximum(trajectory_rmsf(mol).values) < 1e-6\n"
        "setframe!(mol, 1)\n"
        "println(\"trajectory Julia checks passed\")"));
    const bool bridge = script.contains(QStringLiteral("trajectory Julia checks passed"));
    refreshFrameUi();
    const bool reset = slider->value() == 1 && frame->value() == 1;
    play->click();
    QEventLoop events;
    QTimer poll;
    connect(&poll, &QTimer::timeout, &events, [&] { if (!play->isChecked()) events.quit(); });
    poll.start(10);
    QTimer::singleShot(2000, &events, &QEventLoop::quit);
    events.exec();
    const bool playback = !play->isChecked() && frame->value() == 3 && scene_->object(*active)->frameIndex == 2;
    if (play->isChecked()) play->click();
    if (report) {
        *report = QStringLiteral("Timeline seek and selection: %1\nFramebuffer update and rebuild match: %2\n"
                                 "Julia frame access and RMSD: %3\nTimer playback to final frame: %4\n"
                                 "Partial selection upload and stable explorer: %5\n"
                                 "Rapid seeks publish only the latest frame: %6")
                      .arg(seek ? QStringLiteral("passed") : QStringLiteral("failed"),
                           rendered && matchesRebuild ? QStringLiteral("passed") : QStringLiteral("failed"),
                           bridge && reset ? QStringLiteral("passed") : script,
                           playback ? QStringLiteral("passed") : QStringLiteral("failed"),
                           partialSelection ? QStringLiteral("passed") : QStringLiteral("failed"),
                           coalesced ? QStringLiteral("passed") : QStringLiteral("failed"));
        if (!rendered || !matchesRebuild) {
            *report += QStringLiteral("\nSeek framebuffer %1x%2 to %3x%4. Pixels changed: %5. Rebuild matched: %6.")
                .arg(before.width()).arg(before.height()).arg(after.width()).arg(after.height())
                .arg(before != after ? QStringLiteral("yes") : QStringLiteral("no"),
                     matchesRebuild ? QStringLiteral("yes") : QStringLiteral("no"));
        }
    }
    return seek && rendered && matchesRebuild && bridge && reset && playback && partialSelection && coalesced;
}

bool MainWindow::runLinkedAnalysisSelfTest(QString* report) {
    const auto active = scene_->activeObjectId();
    const auto object = active ? scene_->object(*active) : std::nullopt;
    if (!object || !object->trajectory || object->trajectory->frameCount() != 3 ||
        object->molecule->atoms().size() != 3) {
        if (report) *report = QStringLiteral("Expected the three-frame water analysis fixture");
        return false;
    }
    auto* plot = trajectoryAnalysis_->findChild<TrajectoryPlot*>(QStringLiteral("TrajectoryPlot"));
    auto* align = trajectoryAnalysis_->findChild<QCheckBox*>(QStringLiteral("TrajectoryAlign"));
    auto* selectedOnly = trajectoryAnalysis_->findChild<QCheckBox*>(QStringLiteral("TrajectorySelectedOnly"));
    auto* metric = trajectoryAnalysis_->findChild<QComboBox*>(QStringLiteral("TrajectoryMetric"));
    auto* stride = trajectoryAnalysis_->findChild<QSpinBox*>(QStringLiteral("TrajectoryStride"));
    auto* clusters = trajectoryAnalysis_->findChild<QSpinBox*>(QStringLiteral("TrajectoryClusters"));
    auto* calculate = trajectoryAnalysis_->findChild<QPushButton*>(QStringLiteral("TrajectoryCalculate"));
    auto* timeline = trajectoryPanel_->findChild<QSlider*>(QStringLiteral("TrajectorySlider"));
    if (!plot || !align || !selectedOnly || !metric || !stride || !clusters || !calculate || !timeline) {
        if (report) *report = QStringLiteral("Could not locate trajectory analysis controls");
        return false;
    }
    trajectoryAnalysisDock_->show();
    trajectoryAnalysisDock_->raise();
    const auto near = [](double a, double b) { return std::abs(a - b) < 1e-7; };
    const auto click = [plot](std::size_t index) {
        if (index >= plot->values().size()) return;
        const auto position = plot->pointPosition(index);
        QMouseEvent event(QEvent::MouseButtonPress, position, plot->mapToGlobal(position.toPoint()),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(plot, &event);
    };

    align->setChecked(false);
    stride->setValue(2);
    calculate->click();
    const auto values = plot->values();
    const bool sampled = values.size() == 2 && near(values.front(), 0.0) && near(values.back(), 2.0 / std::sqrt(3.0));
    click(1);
    const bool sought = waitForTrajectoryFrame(*active, 2) && timeline->value() == 3 &&
                        plot->markers() == std::vector<std::size_t>{3};
    const auto drawnAtThree = plot->grab().toImage();
    timeline->setValue(2);
    const bool reachedTwo = waitForTrajectoryFrame(*active, 1);
    const auto drawnAtTwo = plot->grab().toImage();
    const bool followed = reachedTwo && plot->values() == values && plot->markers() == std::vector<std::size_t>{2} &&
                          !drawnAtThree.isNull() && drawnAtTwo.size() == drawnAtThree.size() && drawnAtTwo != drawnAtThree;

    const auto oxygen = object->molecule->atoms()[0].id;
    const auto hydrogen = object->molecule->atoms()[1].id;
    selectedOnly->setChecked(true);
    dispatcher_->selectAtom(*active, oxygen);
    calculate->click();
    const bool oxygenScope = plot->values().size() == 2 && near(plot->values().back(), 2.0);
    dispatcher_->selectAtom(*active, hydrogen);
    const bool staleCleared = plot->values().empty();
    QEventLoop events;
    QTimer poll;
    connect(&poll, &QTimer::timeout, &events, [&] { if (!plot->values().empty()) events.quit(); });
    poll.start(10);
    QTimer::singleShot(2000, &events, &QEventLoop::quit);
    events.exec();
    const bool selectionUpdated = plot->values().size() == 2 && near(plot->values().back(), 0.0) &&
                                  scene_->object(*active)->frameIndex == 1;

    selectedOnly->setChecked(false);
    metric->setCurrentIndex(1);
    stride->setValue(1);
    calculate->click();
    const auto fluctuations = plot->values();
    const bool measured = fluctuations.size() == 3 && near(fluctuations[0], std::sqrt(10.0 / 9.0)) &&
                          near(fluctuations[1], 0.0) && near(fluctuations[2], 0.0);
    click(0);
    const bool atomLinked = scene_->selectedAtomId() == oxygen && plot->markers() == std::vector<std::size_t>{1};

    metric->setCurrentIndex(2);
    clusters->setValue(2);
    calculate->click();
    const auto pcs = plot->values();
    const bool projected = pcs.size() == 3 &&
        *std::max_element(pcs.begin(), pcs.end()) - *std::min_element(pcs.begin(), pcs.end()) > 0.1 &&
        plot->groups().size() == 3 && plot->groups().front() != plot->groups().back();
    click(0);
    const bool scatterSought = waitForTrajectoryFrame(*active, 0) && timeline->value() == 1 &&
                               plot->markers() == std::vector<std::size_t>{1};
    const auto pcaAtOne = plot->grab().toImage();
    timeline->setValue(3);
    const bool reachedThree = waitForTrajectoryFrame(*active, 2);
    const auto pcaAtThree = plot->grab().toImage();
    const bool scatterFollowed = reachedThree && plot->values() == pcs && plot->markers() == std::vector<std::size_t>{3} &&
                                 !pcaAtOne.isNull() && pcaAtThree.size() == pcaAtOne.size() && pcaAtThree != pcaAtOne;
    stride->setValue(2);
    calculate->click();
    timeline->setValue(2);
    const bool unsampledPca = waitForTrajectoryFrame(*active, 1) && plot->values().size() == 2 && plot->markers().empty();
    click(1);
    const bool sampledPca = waitForTrajectoryFrame(*active, 2) && timeline->value() == 3 &&
                            plot->markers() == std::vector<std::size_t>{2};
    const bool pcaLink = projected && scatterSought && scatterFollowed && unsampledPca && sampledPca;

    bool contactLink = false;
    if (showContactMap(1.5, false)) {
        const auto beforeMapSelection = analysisMap_->grab().toImage();
        const auto pair = analysisMap_->cellCenter(0, 1);
        QMouseEvent event(QEvent::MouseButtonPress, pair, analysisMap_->mapToGlobal(pair.toPoint()),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(analysisMap_, &event);
        const auto pairMap = analysisMap_->grab().toImage();
        const bool pairSelected = scene_->selectedAtomIds() == std::vector<std::uint64_t>{oxygen, hydrogen} &&
            !pairMap.isNull() && beforeMapSelection.size() == pairMap.size() && pairMap != beforeMapSelection;
        dispatcher_->selectAtom(*active, object->molecule->atoms()[2].id);
        const bool mapFollowedSelection = analysisMap_->grab().toImage() != pairMap;
        timeline->setValue(1);
        const bool framePinned = waitForTrajectoryFrame(*active, 0) && !analysisMapScroll_->isHidden() &&
            analysisSummary_->text().contains(QStringLiteral("Map frame 3 : viewer frame 1"));
        contactLink = pairSelected && mapFollowedSelection && framePinned;
    }

    const auto staticId = scene_->addMolecule(std::make_shared<Molecule>(*object->molecule));
    dispatcher_->setActiveObject(staticId);
    const bool invalidated = plot->values().empty() && !calculate->isEnabled() && analysisMapScroll_->isHidden();
    dispatcher_->closeObject(staticId);

    const bool frameLink = sampled && sought && followed;
    const bool selectionLink = oxygenScope && staleCleared && selectionUpdated;
    const bool atomLink = measured && atomLinked;
    if (report) {
        const auto verdict = [](bool pass) { return pass ? QStringLiteral("passed") : QStringLiteral("failed"); };
        *report = QStringLiteral("Sampled RMSD and bidirectional frame link: %1\n"
                                 "Selected-atom recalculation: %2\nRMSF and atom link: %3\n"
                                 "PCA scatter, clusters, and bidirectional frame link: %4\n"
                                 "Linked contact map and frame label: %5\nObject-switch invalidation: %6")
                      .arg(verdict(frameLink), verdict(selectionLink), verdict(atomLink),
                           verdict(pcaLink), verdict(contactLink), verdict(invalidated));
    }
    return frameLink && selectionLink && atomLink && pcaLink && contactLink && invalidated;
}

bool MainWindow::runPluginSelfTest(const QString& path, QString* report) {
    const auto object = scene_->activeObjectId();
    if (!object) {
        if (report) *report = QStringLiteral("Open a structure for the plugin test.");
        return false;
    }
    QString error;
    if (!loadPlugin(path, &error)) {
        if (report) *report = error;
        return false;
    }
    const bool duplicateRejected = !loadPlugin(path, &error) && error.contains(QStringLiteral("already loaded"));
    const auto menuAction = [this](const QString& menuName, const QString& item) -> QAction* {
        auto* menu = findChild<QMenu*>(menuName);
        if (!menu) return nullptr;
        menu->popup(mapToGlobal(QPoint(40, 40)));
        QCoreApplication::processEvents();
        menu->hide();
        return menu->findChild<QAction*>(item, Qt::FindDirectChildrenOnly);
    };
    const auto commandMenu = QStringLiteral("PluginItems-command");
    const auto commandId = QStringLiteral("structure_tools/space_filling");
    const auto management = QStringLiteral("PluginManagementMenu");
    const auto pluginId = QStringLiteral("structure_tools");
    auto* command = menuAction(commandMenu, commandId);
    if (command) command->trigger();
    QCoreApplication::processEvents();
    const bool commandRan = command && scene_->representation(*object) == Representation::SpaceFilling &&
        statusBar()->currentMessage().contains(QStringLiteral("Showing space-filling atoms"));

    auto* analysis = menuAction(QStringLiteral("PluginItems-analysis"), QStringLiteral("structure_tools/summary"));
    if (analysis) analysis->trigger();
    const bool analysisRan = analysis && analysisDock_->isVisible() &&
        analysisSummary_->text().contains(QStringLiteral("Structure Summary")) &&
        analysisSummary_->text().contains(QStringLiteral("Radius of gyration"));

    const auto before = viewport_->grabFramebuffer();
    auto* color = menuAction(QStringLiteral("PluginItems-color"), QStringLiteral("structure_tools/radial"));
    if (color) color->trigger();
    QCoreApplication::processEvents();
    const auto after = viewport_->grabFramebuffer();
    const auto styled = scene_->object(*object);
    const bool coloringRan = color && styled && styled->atomColors && scene_->colorScheme(*object) == ColorScheme::Custom &&
        !before.isNull() && before.size() == after.size() && before != after;

    auto* toggle = menuAction(management, pluginId);
    const bool enabledAtLoad = toggle && toggle->isChecked();
    if (toggle) toggle->trigger();
    const bool hiddenWhenDisabled = menuAction(commandMenu, commandId) == nullptr &&
        menuAction(QStringLiteral("PluginItems-color"), QStringLiteral("structure_tools/radial")) == nullptr;
    toggle = menuAction(management, pluginId);
    const bool stayedDisabled = toggle && !toggle->isChecked();
    if (toggle) toggle->trigger();
    const bool enableWorked = enabledAtLoad && hiddenWhenDisabled && stayedDisabled && menuAction(commandMenu, commandId);
    if (report) {
        const auto verdict = [](bool ok) { return ok ? QStringLiteral("passed") : QStringLiteral("failed"); };
        *report = QStringLiteral("Local loading and duplicate rejection: %1\nGUI plugin command: %2\n"
                                 "Analysis workspace result: %3\nRendered custom coloring: %4\nEnable/disable menus: %5")
                      .arg(verdict(duplicateRejected), verdict(commandRan), verdict(analysisRan), verdict(coloringRan), verdict(enableWorked));
    }
    return duplicateRejected && commandRan && analysisRan && coloringRan && enableWorked;
}

bool MainWindow::runExportSelfTest(const QString& trajectory, QString* report) {
    const auto fail = [report](const QString& message) {
        if (report) *report = message;
        return false;
    };
    const auto water = scene_->activeObjectId();
    if (!water || scene_->molecule()->atoms().size() != 3 || !attachTrajectory(trajectory)) {
        return fail(QStringLiteral("Open the water topology and analysis trajectory fixtures."));
    }
    QTemporaryDir temporary;
    const auto requestedDirectory = qEnvironmentVariable("JLMOL_EXPORT_TEST_DIR");
    const QDir output(requestedDirectory.isEmpty() ? temporary.path() : requestedDirectory);
    if (!output.exists()) return fail(QStringLiteral("The test export directory does not exist."));

    if (!openStructure(QDir(applicationPaths().project).filePath(QStringLiteral("resources/1CRN.pdb")))) {
        return fail(QStringLiteral("Could not load the 1CRN figure fixture."));
    }
    const auto protein = *scene_->activeObjectId();
    (void)scene_->setObjectVisible(*water, false);
    dispatcher_->setRepresentation(Representation::Cartoon);
    dispatcher_->setColorScheme(ColorScheme::Sequence);
    viewport_->fitToScene();
    dispatcher_->selectAtom(protein, scene_->molecule()->atoms()[30].id);
    viewport_->setLabelMode(LabelMode::SelectedResidues);
    viewport_->setLighting(LightingPreset::Soft);
    viewport_->setBackground(Qt::white);
    (void)scene_->setObjectOpacity(protein, 0.55);

    QString error;
    const FigureOptions figure{{3200, 1800}, true, true};
    const auto pngPath = output.filePath(QStringLiteral("figure.png"));
    if (!saveFigure(*viewport_, pngPath, figure, error)) return fail(error);
    const QImage png(pngPath);
    int translucent = 0;
    for (int y = 0; y < png.height(); y += 2) {
        for (int x = 0; x < png.width(); x += 2) {
            const auto alpha = png.pixelColor(x, y).alpha();
            if (alpha >= 90 && alpha <= 235) ++translucent;
        }
    }
    const bool pngOk = png.size() == figure.pixels && png.pixelColor(0, 0).alpha() == 0 && translucent > 1000 &&
                       std::abs(png.dotsPerMeterX() - 11811) <= 1;
    const QSize compareSize(900, 640);
    const auto labeled = viewport_->renderImage(compareSize, false, true, error);
    const auto unlabeled = viewport_->renderImage(compareSize, false, false, error);
    viewport_->setLighting(LightingPreset::Flat);
    const auto flat = viewport_->renderImage(compareSize, false, false, error);
    const bool appearanceOk = !unlabeled.isNull() && !labeled.isNull() && !flat.isNull() &&
        unlabeled.pixelColor(0, 0) == QColor(Qt::white) && labeled != unlabeled && flat != unlabeled;

    const auto svgPath = output.filePath(QStringLiteral("figure.svg"));
    if (!saveFigure(*viewport_, svgPath, {compareSize, true, true, true}, error)) return fail(error);
    const auto contents = [](const QString& path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    };
    const auto svg = contents(svgPath);
    const bool svgOk = svg.contains("data:image/png;base64,") && svg.contains("<text ") && svg.contains("viewBox=\"0 0 900 640\"");

    dispatcher_->closeObject(protein);
    dispatcher_->setObjectVisible(*water, true);
    dispatcher_->setActiveObject(*water);
    dispatcher_->setRepresentation(Representation::SpaceFilling);
    dispatcher_->setColorScheme(ColorScheme::Element);
    dispatcher_->selectAtom(std::nullopt);
    viewport_->setLabelMode(LabelMode::None);
    viewport_->fitToScene();
    if (!dispatcher_->setTrajectoryFrame(*water, 1)) return fail(QStringLiteral("Could not set the live trajectory frame."));
    const auto object = *scene_->object(*water);
    const auto generation = scene_->generation();
    const auto camera = viewport_->cameraState();
    const FigureOptions animation{{640, 480}, true, false};
    const auto liveImage = viewport_->renderImage(animation.pixels, true, false, error);
    const auto animationPath = output.filePath(QStringLiteral("animation.png"));
    if (!exportTrajectory(animationPath, object, animation, 0, 1, 3, 12, [](int) { return false; }, error)) return fail(error);
    const auto apng = contents(animationPath);
    const QImage firstFrame(animationPath);
    const auto afterExport = viewport_->renderImage(animation.pixels, true, false, error);
    const bool animationOk = firstFrame.size() == animation.pixels && apng.contains("acTL") && apng.contains("fdAT") &&
        !afterExport.isNull() && !liveImage.isNull() && afterExport == liveImage && scene_->generation() == generation &&
        scene_->object(*water)->frameIndex == 1 && viewport_->cameraState() == camera;
    const bool cancelled = !exportTrajectory(animationPath, object, animation, 0, 1, 3, 12,
        [](int frame) { return frame == 1; }, error) && error.isEmpty() && contents(animationPath) == apng;
    const bool invalidSize = viewport_->renderImage(QSize(8192, 8192), true, false, error).isNull() && !error.isEmpty();
    if (report) {
        const auto verdict = [](bool pass) { return pass ? QStringLiteral("passed") : QStringLiteral("failed"); };
        *report = QStringLiteral("High-resolution PNG with partial alpha and print metadata: %1\n"
                                 "Background, labels, and lighting: %2\nSVG with editable labels: %3\n"
                                 "Animation and unchanged live frame: %4\nCancelled writes and oversized images: %5")
                      .arg(verdict(pngOk), verdict(appearanceOk), verdict(svgOk), verdict(animationOk), verdict(cancelled && invalidSize));
    }
    return pngOk && appearanceOk && svgOk && animationOk && cancelled && invalidSize;
}

bool MainWindow::runWorkspaceSelfTest(const QString& trajectory, QString* report) {
    const auto fail = [report](const QString& message) {
        if (report) *report = message;
        return false;
    };
    const auto initial = scene_->activeObjectId();
    const auto molecule = scene_->molecule();
    if (!initial || !molecule || molecule->atoms().size() != 3) return fail(QStringLiteral("Open the water topology fixture."));
    QTemporaryDir temporary;
    QDir parent(temporary.path());
    if (!temporary.isValid() || !parent.mkdir(QStringLiteral("original"))) return fail(QStringLiteral("Could not prepare test files."));
    const auto original = parent.filePath(QStringLiteral("original/"));
    const auto topology = original + QStringLiteral("water λ.xyz");
    const auto reference = original + QStringLiteral("reference.xyz");
    const auto frames = original + QStringLiteral("frames.xyz");
    const auto source = QFileInfo(molecule->sourcePath()).absoluteFilePath();
    if (!QFile::copy(source, topology) || !QFile::copy(source, reference) || !QFile::copy(trajectory, frames)) {
        return fail(QStringLiteral("Could not copy the water fixtures."));
    }
    dispatcher_->closeObject(*initial);
    QString message;
    const auto emptyProject = original + QStringLiteral("empty.jlmol");
    if (!saveWorkspace(emptyProject, message) || !openWorkspace(emptyProject, message)) return fail(message);
    if (scene_->objectCount() != 0) return fail(QStringLiteral("Empty workspace unexpectedly contains structures."));
    if (!openStructure(topology) || !attachTrajectory(frames)) return fail(QStringLiteral("Could not load the copied trajectory."));
    const auto movingId = *scene_->activeObjectId();
    if (!openStructure(reference)) return fail(QStringLiteral("Could not load the reference structure."));
    const auto referenceId = *scene_->activeObjectId();
    const auto& atoms = molecule->atoms();
    const std::vector<std::uint64_t> selected{atoms[1].id, atoms[0].id};
    const std::vector<float> rgb{0.2F, 0.5F, 0.9F, 0.3F, 0.6F, 0.7F, 0.8F, 0.2F, 0.1F};
    scene_->setAtomColors(movingId, rgb);
    (void)scene_->setRepresentation(movingId, Representation::SpaceFilling);
    (void)scene_->setObjectOpacity(movingId, 0.625);
    (void)scene_->setObjectVisible(referenceId, false);
    (void)scene_->setColorScheme(referenceId, ColorScheme::Chain);
    dispatcher_->selectAtoms(movingId, selected);
    if (!showContactMap(1.5, false) || !dispatcher_->setTrajectoryFrame(movingId, 2)) {
        return fail(QStringLiteral("Could not prepare the pinned contact map."));
    }
    const TrajectoryAnalysisPanel::Settings analysis{TrajectoryAnalysisPanel::Metric::Pca, false, true, 2, 2, 2, true};
    if (!trajectoryAnalysis_->restoreSettings(analysis)) return fail(QStringLiteral("Could not calculate the original PCA plot."));
    trajectoryPanel_->restorePlaybackRate(19);
    const MolecularViewport::CameraState camera{QQuaternion::fromEulerAngles(12.F, 44.F, 8.F),
        QVector3D(0.25F, -0.75F, 0.F), QVector3D(0.5F, 0.25F, 1.F), 17.F, 4.F, true, LabelMode::SelectedAtoms,
        QColor(250, 250, 245), LightingPreset::Soft};
    viewport_->restoreCamera(camera);
    addDockWidget(Qt::RightDockWidgetArea, trajectoryDock_);
    trajectoryDock_->hide();
    trajectoryAnalysisDock_->show();
    QCoreApplication::processEvents();

    const bool protectedInput = !saveWorkspace(topology, message) && message.contains(QStringLiteral("separate"));
    if (!saveWorkspace(original + QStringLiteral("project.jlmol"), message)) return fail(message);
    dispatcher_->setActiveObject(referenceId);
    viewport_->setViewPreset(ViewPreset::Top);
    viewport_->setOrthographic(false);
    viewport_->setBackground(Qt::black);
    viewport_->setLighting(LightingPreset::Flat);
    addDockWidget(Qt::LeftDockWidgetArea, trajectoryDock_);
    trajectoryDock_->show();
    trajectoryAnalysisDock_->hide();
    if (!parent.rename(QStringLiteral("original"), QStringLiteral("moved"))) return fail(QStringLiteral("Could not relocate the workspace."));
    const auto moved = parent.filePath(QStringLiteral("moved/"));
    const auto project = moved + QStringLiteral("project.jlmol");
    if (!openWorkspace(project, message)) return fail(message);
    QCoreApplication::processEvents();

    const auto restored = scene_->objects();
    if (restored.size() != 2) return fail(QStringLiteral("Workspace did not restore both structures."));
    const auto& first = restored[0];
    const auto& second = restored[1];
    const bool sceneMatches = scene_->activeObjectId() == first.id && scene_->selectedObjectId() == first.id &&
        scene_->selectedAtomIds() == selected && !scene_->object(movingId) && !scene_->object(referenceId) &&
        first.visible && !second.visible && first.opacity == 0.625 && first.representation == Representation::SpaceFilling &&
        first.colorScheme == ColorScheme::Custom && first.atomColors && (*first.atomColors)[1][2] == rgb[5] &&
        second.colorScheme == ColorScheme::Chain && first.trajectory && first.frameIndex == 2 &&
        first.molecule->atoms()[0].position.z == 2.F && trajectoryPanel_->playbackRate() == 19;
    const bool viewMatches = viewport_->cameraState() == camera && projectionActions_->checkedAction()->data().toBool() &&
        labelActions_->checkedAction()->data().toInt() == static_cast<int>(camera.labels) &&
        lightingActions_->checkedAction()->data().toInt() == static_cast<int>(camera.lighting) &&
        dockWidgetArea(trajectoryDock_) == Qt::RightDockWidgetArea && trajectoryDock_->isHidden() &&
        !trajectoryAnalysisDock_->isHidden() && !analysisDock_->isHidden();
    const bool analysisMatches = trajectoryAnalysis_->settings() == analysis && contactMap_ && contactMap_->frame == 0 &&
        contactMap_->cutoff == 1.5 && !contactMap_->cAlphaOnly &&
        analysisSummary_->text().contains(QStringLiteral("Map frame 1 : viewer frame 3"));

    QFile saved(project);
    if (!saved.open(QIODevice::ReadOnly)) return fail(QStringLiteral("Could not inspect the saved workspace."));
    auto invalid = QJsonDocument::fromJson(saved.readAll()).object();
    saved.close();
    invalid["layout"] = QString::fromLatin1(QByteArray("invalid Qt layout").toBase64());
    QFile malformed(moved + QStringLiteral("bad-layout.jlmol"));
    const auto invalidBytes = QJsonDocument(invalid).toJson();
    if (!malformed.open(QIODevice::WriteOnly) || malformed.write(invalidBytes) != invalidBytes.size()) {
        return fail(QStringLiteral("Could not prepare the corrupt workspace."));
    }
    malformed.close();
    const auto generation = scene_->generation();
    const auto layout = saveState(WorkspaceSettings::layoutVersion);
    const bool rejectedLayout = !openWorkspace(malformed.fileName(), message) && message.contains(QStringLiteral("panel layout"));
    const auto movedFrames = moved + QStringLiteral("frames.xyz");
    if (!QFile::rename(movedFrames, movedFrames + QStringLiteral(".missing"))) return fail(QStringLiteral("Could not hide the trajectory fixture."));
    const bool rejectedMissing = !openWorkspace(project, message) && message.contains(QStringLiteral("trajectory file")) &&
        message.contains(QStringLiteral("frames.xyz"));
    const bool preserved = scene_->generation() == generation && viewport_->cameraState() == camera &&
        saveState(WorkspaceSettings::layoutVersion) == layout;
    if (!QFile::rename(movedFrames + QStringLiteral(".missing"), movedFrames)) return fail(QStringLiteral("Could not restore the trajectory fixture."));

    const auto alignmentProject = moved + QStringLiteral("alignment.jlmol");
    // The two hydrogens in the XYZ topology share the same atom identity. Use a PDB for alignment.
    const auto peptide = QFileInfo(source).dir().filePath(QStringLiteral("mini.pdb"));
    if (!openStructure(peptide)) return fail(QStringLiteral("Could not open the alignment reference fixture."));
    const auto peptideReference = *scene_->activeObjectId();
    if (!openStructure(peptide) || !showAlignment(peptideReference)) {
        return fail(QStringLiteral("Could not calculate the alignment fixture."));
    }
    if (!saveWorkspace(alignmentProject, message)) return fail(message);
    dispatcher_->setActiveObject(second.id);
    if (!openWorkspace(alignmentProject, message)) return fail(message);
    const auto aligned = scene_->objects();
    const bool alignmentMatches = aligned.size() == 4 && alignmentReference_ == aligned[2].id &&
        analysisSummary_->text().contains(QStringLiteral("RMSD after alignment:"));

    if (report) {
        const auto verdict = [](bool passed) { return passed ? QStringLiteral("passed") : QStringLiteral("failed"); };
        *report = QStringLiteral("Relocated files, object styles, ordered selection, and paused frame: %1\n"
                                 "Camera and dock layout: %2\nPCA and pinned contact map: %3\n"
                                 "Referenced inputs protected and failed opens preserve state: %4\nAlignment settings: %5")
            .arg(verdict(sceneMatches), verdict(viewMatches), verdict(analysisMatches),
                 verdict(protectedInput && rejectedLayout && rejectedMissing && preserved), verdict(alignmentMatches));
    }
    return sceneMatches && viewMatches && analysisMatches && protectedInput && rejectedLayout && rejectedMissing && preserved && alignmentMatches;
}

bool MainWindow::runScriptEditorSelfTest(QString* report) {
    if (!scriptEditor_ || !juliaRuntime_.isAvailable()) {
        if (report) *report = QStringLiteral("Script editor or Julia runtime is unavailable");
        return false;
    }
    const QString source = QStringLiteral("editor_probe = 40\neditor_probe + 2");
    scriptEditor_->setCurrentText(source);
    scriptEditor_->currentEditor()->selectAll();
    const QString selectionResult = scriptEditor_->runSelection();

    QTemporaryDir directory;
    QString fileError;
    const QString path = directory.filePath(QStringLiteral("roundtrip.jl"));
    const bool saved = directory.isValid() && scriptEditor_->saveCurrentTo(path, &fileError);
    scriptEditor_->setCurrentText(QStringLiteral("replaced locally"));
    const bool opened = saved && scriptEditor_->openFile(path, &fileError);
    const bool roundTripped = opened && scriptEditor_->currentText() == source;
    const int replacements = scriptEditor_->replaceAllText(QStringLiteral("editor_probe"), QStringLiteral("editor_value"));
    const bool replaced = replacements == 2 && scriptEditor_->currentText().contains(QStringLiteral("editor_value"));

    scriptEditor_->setCurrentText(QStringLiteral("editor_probe + 1"));
    const QString persistenceResult = scriptEditor_->runScript();
    scriptEditor_->setCurrentText(QStringLiteral("error(\"intentional editor error\")"));
    const QString errorResult = scriptEditor_->runScript();
    const bool passed = selectionResult.contains(QStringLiteral("42")) && roundTripped && replaced &&
                        persistenceResult.contains(QStringLiteral("41")) &&
                        errorResult.contains(QStringLiteral("intentional editor error"));
    if (report) {
        *report = QStringLiteral("selection: %1\nround trip: %2\nreplace all: %3\npersistence: %4\nerror: %5%6")
                      .arg(selectionResult, roundTripped ? QStringLiteral("passed") : QStringLiteral("failed"),
                           replaced ? QStringLiteral("passed") : QStringLiteral("failed"), persistenceResult, errorResult,
                           fileError.isEmpty() ? QString{} : QStringLiteral("\nfile error: ") + fileError);
    }
    return passed;
}

void MainWindow::chooseStructure() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open molecular structure"), {},
                                                      QStringLiteral("Molecular structures (*.pdb *.ent *.cif *.mmcif *.xyz *.mol *.sdf *.mol2 *.gro *.pqr);;All files (*)"));
    if (!path.isEmpty()) openStructure(path);
}

void MainWindow::chooseTrajectory() {
    if (!scene_->activeObjectId()) {
        showError(QStringLiteral("Open the matching structure before attaching a trajectory."));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Attach trajectory to active structure"), {},
        QStringLiteral("Trajectories (*.xtc *.trr *.dcd *.xyz);;All files (*)"));
    if (!path.isEmpty()) attachTrajectory(path);
}

void MainWindow::chooseWorkspaceToOpen() {
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Open Workspace"), {},
                                                   QStringLiteral("JlMol workspaces (*.jlmol)"));
    if (path.isEmpty()) return;
    QString message;
    if (!openWorkspace(path, message)) showError(message);
    else statusBar()->showMessage(message, 10000);
}

void MainWindow::chooseWorkspaceToSave() {
    QFileDialog dialog(this, QStringLiteral("Save Workspace"), {}, QStringLiteral("JlMol workspaces (*.jlmol)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix(QStringLiteral("jlmol"));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
    QString message;
    if (!saveWorkspace(dialog.selectedFiles().first(), message)) showError(message);
    else statusBar()->showMessage(message, 6000);
}

void MainWindow::chooseFigureExport() {
    if (!scene_->objectCount()) {
        showError(QStringLiteral("Open a structure before exporting a figure."));
        return;
    }
    const auto request = chooseExportOptions(this, viewport_->size());
    if (!request) return;
    const bool svg = request->figure.svg;
    const auto suffix = svg ? QStringLiteral("svg") : QStringLiteral("png");
    const auto path = chooseExportPath(this, QStringLiteral("Export Figure"),
        svg ? QStringLiteral("SVG files (*.svg)") : QStringLiteral("PNG images (*.png)"), suffix);
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().compare(suffix, Qt::CaseInsensitive) != 0) {
        showError(QStringLiteral("Use a .%1 filename for this figure.").arg(suffix));
        return;
    }
    QString error;
    if (!saveFigure(*viewport_, path, request->figure, error)) showError(error);
    else statusBar()->showMessage(QStringLiteral("Exported %1").arg(QFileInfo(path).fileName()), 6000);
}

void MainWindow::chooseAnimationExport() {
    const auto id = scene_->activeObjectId();
    const auto object = id ? scene_->object(*id) : std::nullopt;
    if (!object || !object->trajectory || !object->visible || object->opacity <= 0.0) {
        showError(QStringLiteral("Select a visible structure with an attached trajectory before exporting an animation."));
        return;
    }
    const auto frames = object->trajectory->frameCount();
    if (frames > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        showError(QStringLiteral("This trajectory has too many frames for the animation controls."));
        return;
    }
    const auto request = chooseExportOptions(this, viewport_->size(), static_cast<int>(frames), trajectoryPanel_->playbackRate());
    if (!request) return;
    const auto path = chooseExportPath(this, QStringLiteral("Export Animation"), QStringLiteral("Animated PNG (*.png)"), QStringLiteral("png"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().compare(QStringLiteral("png"), Qt::CaseInsensitive) != 0) {
        showError(QStringLiteral("Use a .png filename for the animation."));
        return;
    }
    trajectoryPanel_->pause();
    viewport_->restoreCamera(viewport_->cameraState());
    QProgressDialog progress(QStringLiteral("Rendering trajectory frames…"), QStringLiteral("Cancel"), 0, request->count, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    const auto cancelled = [&progress](int frame) {
        progress.setValue(frame);
        return progress.wasCanceled();
    };
    QString error;
    const bool saved = exportTrajectory(path, *object, request->figure, request->first, request->stride, request->count,
                                        request->fps, cancelled, error);
    progress.hide();
    if (!saved && !error.isEmpty()) showError(error);
    else if (saved) statusBar()->showMessage(QStringLiteral("Exported %1").arg(QFileInfo(path).fileName()), 6000);
    else statusBar()->showMessage(QStringLiteral("Animation export cancelled."), 6000);
}

bool MainWindow::exportTrajectory(const QString& path, const MolecularObject& object, const FigureOptions& options,
                                  int first, int step, int count, int fps, const std::function<bool(int)>& cancelled,
                                  QString& error) {
    try {
        auto frameObject = object;
        return saveAnimatedPng(path, options.pixels, count, fps, [&](int index) -> QImage {
            if (cancelled(index)) return {};
            const auto frame = object.trajectory->readFrame(static_cast<std::size_t>(first) + std::size_t(index) * step);
            frameObject.molecule = std::make_shared<Molecule>(object.molecule->withPositions(frame->positions));
            return viewport_->renderImage(options.pixels, options.transparent, options.labels, error, &frameObject);
        }, error);
    } catch (const std::exception& exception) {
        error = QString::fromUtf8(exception.what());
        return false;
    }
}

bool MainWindow::saveWorkspace(const QString& path, QString& message) {
    try {
        WorkspaceSettings settings{viewport_->cameraState(), saveState(WorkspaceSettings::layoutVersion),
                                   trajectoryAnalysis_->settings(), trajectoryPanel_->playbackRate(), {}, {}};
        if (contactMap_ && scene_->activeObjectId() == contactMap_->object) {
            settings.contactMap = WorkspaceContactMap{contactMap_->cutoff, contactMap_->cAlphaOnly, contactMap_->frame};
        }
        if (alignmentReference_) {
            const auto objects = scene_->objects();
            const auto reference = std::find_if(objects.begin(), objects.end(), [this](const auto& object) {
                return object.id == *alignmentReference_;
            });
            if (reference != objects.end()) settings.alignmentReference = reference - objects.begin();
        }
        writeWorkspace(path, *scene_, settings);
        message = QStringLiteral("Saved workspace %1").arg(QFileInfo(path).fileName());
        return true;
    } catch (const std::exception& error) {
        message = QString::fromUtf8(error.what());
        return false;
    }
}

bool MainWindow::openWorkspace(const QString& path, QString& message) {
    MolecularScene staged;
    WorkspaceSettings settings{};
    try {
        settings = readWorkspace(path, staged);
    } catch (const std::exception& error) {
        message = QString::fromUtf8(error.what());
        return false;
    }

    const auto previousLayout = saveState(WorkspaceSettings::layoutVersion);
    if (!restoreState(settings.layout, WorkspaceSettings::layoutVersion)) {
        (void)restoreState(previousLayout, WorkspaceSettings::layoutVersion);
        message = QStringLiteral("This workspace contains an invalid or incompatible panel layout.");
        return false;
    }
    scene_->replaceWith(std::move(staged));
    clearContactMap();
    viewport_->restoreCamera(settings.camera);
    for (auto* action : projectionActions_->actions()) {
        if (action->data().toBool() == settings.camera.orthographic) action->setChecked(true);
    }
    for (auto* action : labelActions_->actions()) {
        if (action->data().toInt() == static_cast<int>(settings.camera.labels)) action->setChecked(true);
    }
    for (auto* action : lightingActions_->actions()) {
        if (action->data().toInt() == static_cast<int>(settings.camera.lighting)) action->setChecked(true);
    }
    trajectoryPanel_->restorePlaybackRate(settings.playbackRate);
    viewport_->sceneChanged();
    refreshSceneUi();

    bool analysisRestored = trajectoryAnalysis_->restoreSettings(settings.trajectoryAnalysis);
    if (settings.contactMap) {
        const auto& map = *settings.contactMap;
        analysisRestored = showContactMap(map.cutoff, map.cAlphaOnly, map.frame, false) && analysisRestored;
    } else if (settings.alignmentReference) {
        const auto reference = scene_->objects()[*settings.alignmentReference].id;
        analysisRestored = showAlignment(reference, false) && analysisRestored;
    }
    message = QStringLiteral("Opened workspace %1").arg(QFileInfo(path).fileName());
    if (!analysisRestored) message += QStringLiteral(". Some analyses could not be recalculated.");
    return true;
}

void MainWindow::refreshSceneUi() {
    const QSignalBlocker blocker(objectTree_);
    const auto objects = scene_->objects();
    const auto activeId = scene_->activeObjectId();
    if (objects.empty()) {
        objectTree_->clear();
        atomCountLabel_->setText(QStringLiteral("No structure loaded"));
        refreshFrameUi();
        return;
    }
    std::size_t atomTotal = 0;
    std::size_t bondTotal = 0;
    std::unordered_map<ObjectId, QTreeWidgetItem*> previousRoots;
    for (int i = 0; i < objectTree_->topLevelItemCount(); ++i) {
        auto* root = objectTree_->topLevelItem(i);
        previousRoots.emplace(root->data(0, objectIdRole).toULongLong(), root);
    }
    QTreeWidgetItem* activeRoot = nullptr;
    for (const auto& object : objects) {
        if (!object.molecule) continue;
        const auto& molecule = *object.molecule;
        atomTotal += molecule.atoms().size();
        bondTotal += molecule.bonds().size();
        const auto previous = previousRoots.extract(object.id);
        auto* root = previous.empty() ? new QTreeWidgetItem(objectTree_, {QString::fromStdString(molecule.name())})
                                      : previous.mapped();
        if (activeId == object.id) activeRoot = root;
        root->setCheckState(0, object.visible ? Qt::Checked : Qt::Unchecked);
        root->setToolTip(0, QStringLiteral("%1\nOpacity: %2%")
                               .arg(QString::fromStdString(molecule.sourcePath().string()))
                               .arg(object.opacity * 100.0, 0, 'g', 4));
        if (!previous.empty()) continue;
        // Frame changes keep the object's topology and explorer children intact.
        root->setData(0, objectIdRole, QVariant::fromValue<qulonglong>(object.id));
        root->setData(0, itemKindRole, objectItemKind);
        root->setFlags(root->flags() | Qt::ItemIsUserCheckable);
        root->setExpanded(objects.size() == 1);
        std::map<std::string, std::map<std::pair<std::string, int>, int>> hierarchy;
        for (const auto& atom : molecule.atoms()) hierarchy[atom.chainId][{atom.residueName, atom.residueId}]++;
        for (const auto& [chain, residues] : hierarchy) {
            auto* chainItem = new QTreeWidgetItem(root, {QStringLiteral("Chain %1").arg(QString::fromStdString(chain.empty() ? "(blank)" : chain))});
            chainItem->setData(0, itemKindRole, chainItemKind);
            chainItem->setData(0, chainRole, QString::fromStdString(chain));
            for (const auto& [residue, count] : residues) {
                auto* residueEntry = new QTreeWidgetItem(chainItem, {QStringLiteral("%1 %2  (%3 atoms)")
                                                                        .arg(QString::fromStdString(residue.first))
                                                                        .arg(residue.second)
                                                                        .arg(count)});
                residueEntry->setData(0, itemKindRole, residueItemKind);
                residueEntry->setData(0, chainRole, QString::fromStdString(chain));
                residueEntry->setData(0, residueNameRole, QString::fromStdString(residue.first));
                residueEntry->setData(0, residueIdRole, residue.second);
            }
        }
    }
    for (const auto& [id, root] : previousRoots) delete root;
    if (objectTree_->currentItem() != activeRoot) objectTree_->setCurrentItem(activeRoot);
    atomCountLabel_->setText(QStringLiteral("%1 objects : %2 atoms : %3 bonds : %4 : color %5")
                                 .arg(objects.size())
                                 .arg(atomTotal)
                                 .arg(bondTotal)
                                 .arg(QString::fromStdString(toString(scene_->representation())))
                                 .arg(QString::fromStdString(toString(scene_->colorScheme()))));
    refreshFrameUi();
}

void MainWindow::refreshFrameUi() {
    trajectoryPanel_->refresh();
    trajectoryAnalysis_->refresh();
    if (refreshContactMap()) {
        updateInspector();
        return;
    }
    alignmentReference_.reset();
    if (analysisDock_->isVisible()) refreshDefaultAnalysis();
    else defaultAnalysisPending_ = true;
    updateInspector();
}

void MainWindow::refreshDefaultAnalysis() {
    defaultAnalysisPending_ = false;
    if (const auto molecule = scene_->molecule()) {
        if (molecule != radiusSource_.lock()) {
            cachedRadius_ = analysis::radiusOfGyration(*molecule);
            radiusSource_ = molecule;
        }
        analysisSummary_->setText(QStringLiteral("<h3>%1</h3><p>Radius of gyration: <b>%2 Å</b></p>"
                                                "<p>Shift-click 2, 3, or 4 atoms for distance, angle, or dihedral.</p>")
                                      .arg(QString::fromStdString(molecule->name()))
                                      .arg(cachedRadius_, 0, 'f', 3));
    } else {
        analysisSummary_->setText(QStringLiteral("Open a structure to calculate geometric properties."));
    }
}

void MainWindow::updateInspector() {
    const auto atoms = scene_->selectedAtoms();
    if (atoms.empty()) {
        inspector_->setText(QStringLiteral("Select an atom in the viewport, or a chain or residue in the explorer. Shift-click up to four atoms to measure distance, angle, or dihedral."));
        return;
    }
    if (atoms.size() > 1) {
        QString measurement;
        if (atoms.size() == 2) {
            measurement = QStringLiteral("<h3>Distance: %1 Å</h3><p>%2 (%3) to %4 (%5)</p>")
                              .arg(analysis::distance(atoms[0].position, atoms[1].position), 0, 'f', 3)
                              .arg(QString::fromStdString(atoms[0].name)).arg(atoms[0].serial)
                              .arg(QString::fromStdString(atoms[1].name)).arg(atoms[1].serial);
        } else if (atoms.size() == 3) {
            const auto angle = analysis::angleDegrees(atoms[0].position, atoms[1].position, atoms[2].position);
            measurement = angle ? QStringLiteral("<h3>Angle: %1°</h3><p>Atoms %2 : %3 : %4</p>")
                                      .arg(*angle, 0, 'f', 2)
                                      .arg(atoms[0].serial).arg(atoms[1].serial).arg(atoms[2].serial)
                                : QStringLiteral("<h3>Angle is undefined</h3><p>Two selected positions coincide.</p>");
        } else if (atoms.size() == 4) {
            const auto dihedral = analysis::dihedralDegrees(atoms[0].position, atoms[1].position,
                                                             atoms[2].position, atoms[3].position);
            measurement = dihedral ? QStringLiteral("<h3>Dihedral: %1°</h3><p>Atoms %2 : %3 : %4 : %5</p>")
                                         .arg(*dihedral, 0, 'f', 2)
                                         .arg(atoms[0].serial).arg(atoms[1].serial)
                                         .arg(atoms[2].serial).arg(atoms[3].serial)
                                   : QStringLiteral("<h3>Dihedral is undefined</h3><p>Three selected positions are collinear.</p>");
        }
        inspector_->setText(QStringLiteral("<h3>%1 atoms selected</h3>%2<p>%3 %4 : chain %5</p>")
                                .arg(atoms.size()).arg(measurement)
                                .arg(QString::fromStdString(atoms.front().residueName)).arg(atoms.front().residueId)
                                .arg(QString::fromStdString(atoms.front().chainId)));
        if (analysisDock_->isVisible() && !contactMap_ && !measurement.isEmpty()) analysisSummary_->setText(measurement);
        return;
    }
    const auto& atom = atoms.front();
    inspector_->setText(QStringLiteral("<h3>%1</h3><table>"
                                       "<tr><td>Element</td><td><b>%2</b></td></tr>"
                                       "<tr><td>Serial</td><td>%3</td></tr>"
                                       "<tr><td>Residue</td><td>%4 %5</td></tr>"
                                       "<tr><td>Chain</td><td>%6</td></tr>"
                                       "<tr><td>Secondary structure</td><td>%7</td></tr>"
                                       "<tr><td>Coordinates</td><td>%8, %9, %10 Å</td></tr>"
                                       "<tr><td>Occupancy</td><td>%11</td></tr>"
                                       "<tr><td>B-factor</td><td>%12 Å²</td></tr></table>")
                            .arg(QString::fromStdString(atom.name))
                            .arg(QString::fromStdString(atom.element))
                            .arg(atom.serial)
                            .arg(QString::fromStdString(atom.residueName))
                            .arg(atom.residueId)
                            .arg(QString::fromStdString(atom.chainId))
                            .arg(QString::fromStdString(secondaryStructureName(atom.secondaryStructure)))
                            .arg(atom.position.x, 0, 'f', 3)
                            .arg(atom.position.y, 0, 'f', 3)
                            .arg(atom.position.z, 0, 'f', 3)
                            .arg(atom.occupancy, 0, 'f', 2)
                            .arg(atom.bFactor, 0, 'f', 2));
}

void MainWindow::showError(const QString& message) {
    statusBar()->showMessage(message, 10000);
    QMessageBox::critical(this, QStringLiteral("JlMol"), message);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (scriptEditor_ && !scriptEditor_->maybeSaveAll()) {
        event->ignore();
        return;
    }
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

