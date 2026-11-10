#include "gui/MainWindow.h"

#include "bridge/JlMolAPI.h"
#include "core/Analysis.h"
#include "gui/JuliaConsole.h"
#include "gui/MolecularViewport.h"
#include "gui/ScriptEditor.h"
#include "gui/TrajectoryPanel.h"

#include <QActionGroup>
#include <QCloseEvent>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMetaObject>
#include <QSettings>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

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
            {QStringLiteral("By Secondary Structure"), ColorScheme::SecondaryStructure}};
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
    analysisMap_ = new QLabel(analysis);
    analysisMap_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    analysisMapScroll_ = new QScrollArea(analysis);
    analysisMapScroll_->setWidget(analysisMap_);
    analysisMapScroll_->setWidgetResizable(true);
    analysisLayout->addWidget(analysisMapScroll_);
    analysisMapScroll_->hide();
    analysisMapScroll_->setMinimumHeight(160);
    analysisLayout->addStretch();
    analysisDock_ = makeDock(this, QStringLiteral("Analysis Workspace"), QStringLiteral("AnalysisDock"), analysis, Qt::BottomDockWidgetArea);
    analysisDock_->hide();
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
    auto* projectionGroup = new QActionGroup(this);
    projectionGroup->addAction(perspective);
    projectionGroup->addAction(orthographic);
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
    auto* labelGroup = new QActionGroup(this);
    const std::pair<QString, LabelMode> labelModes[] = {
        {QStringLiteral("None"), LabelMode::None},
        {QStringLiteral("Selected Atoms"), LabelMode::SelectedAtoms},
        {QStringLiteral("Selected Residues"), LabelMode::SelectedResidues}};
    for (const auto& [label, mode] : labelModes) {
        auto* action = labels->addAction(label);
        action->setCheckable(true);
        labelGroup->addAction(action);
        if (mode == LabelMode::None) action->setChecked(true);
        connect(action, &QAction::triggered, this, [this, mode] { viewport_->setLabelMode(mode); });
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

    auto* analysisMenu = menuBar()->addMenu(QStringLiteral("&Analysis"));
    analysisMenu->addAction(analysisDock_->toggleViewAction());
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
        analysisMapScroll_->hide();
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

    auto* toolbar = addToolBar(QStringLiteral("Main"));
    toolbar->setObjectName(QStringLiteral("MainToolbar"));
    toolbar->addAction(open);
    toolbar->addAction(QStringLiteral("Fit"), viewport_, &MolecularViewport::fitToScene);
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

bool MainWindow::showAlignment(ObjectId referenceId) {
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
    analysisDock_->show();
    analysisDock_->raise();
    analysisMapScroll_->hide();
    analysisSummary_->setText(QStringLiteral("<h3>Structural alignment</h3><pre>%1</pre>").arg(output.toHtmlEscaped()));
    return output.contains(QStringLiteral("RMSD after alignment:"));
}

bool MainWindow::showContactMap(double cutoff, bool cAlphaOnly) {
    const auto active = scene_->activeObjectId();
    if (!active) return false;
    // A returned Julia string avoids filling the redirected stdout pipe with matrix data.
    const auto output = juliaRuntime_.evaluate(QStringLiteral(
        "using JlMol\nlet data = JlMol.snapshot(JlMol.ActiveStructure(UInt64(%1)))\n"
        "indices = %2\n"
        "isempty(indices) && error(\"No atoms match this contact-map selection\")\n"
        "length(indices) <= 1024 || error(\"GUI contact maps support up to 1024 atoms; choose C-alpha atoms or use the Julia API\")\n"
        "map = JlMol.contactmap(data.coordinates[:, indices]; cutoff=%3)\n"
        "join([join(data.atom_ids[indices], \",\"); [join(ifelse.(map[i, :], \"1\", \"0\")) for i in axes(map, 1)]], \"\\n\")\n"
        "end").arg(*active)
        .arg(cAlphaOnly ? QStringLiteral("findall(==(\"CA\"), data.atom_names)")
                       : QStringLiteral("collect(eachindex(data.atom_ids))"))
        .arg(cutoff, 0, 'g', 15));
    const auto decoded = QJsonDocument::fromJson((QStringLiteral("[") + output + QStringLiteral("]")).toUtf8());
    const auto rows = decoded.isArray() && decoded.array().size() == 1 && decoded.array()[0].isString()
                          ? decoded.array()[0].toString().split('\n') : QStringList{};
    analysisDock_->show();
    analysisDock_->raise();
    analysisMapScroll_->hide();
    if (rows.size() < 2) {
        analysisSummary_->setText(QStringLiteral("<h3>Contact map</h3><pre>%1</pre>").arg(output.toHtmlEscaped()));
        return false;
    }
    const auto ids = rows.front().split(',');
    const int count = ids.size();
    if (count > 1024 || rows.size() != count + 1) return false;
    QImage image(count, count, QImage::Format_RGB32);
    int contacts = 0;
    for (int row = 0; row < count; ++row) {
        if (rows[row + 1].size() != count) return false;
        for (int column = 0; column < count; ++column) {
            const bool contact = rows[row + 1][column] == QLatin1Char('1');
            image.setPixelColor(column, row, contact ? QColor(44, 162, 170) : QColor(245, 247, 250));
            if (row < column && contact) ++contacts;
        }
    }
    analysisSummary_->setText(QStringLiteral("<h3>Contact map</h3><p>%1 atoms : %2 unique contacts : cutoff %3 Å</p>"
        "<p>Rows and columns follow atom order. Teal: contact; pale: no contact. Diagonal excluded; nonperiodic distances.</p>")
                                 .arg(count).arg(contacts).arg(cutoff));
    analysisMap_->setPixmap(QPixmap::fromImage(image.scaled(512, 512, Qt::KeepAspectRatio, Qt::FastTransformation)));
    analysisMap_->setToolTip(QStringLiteral("Ordered atom IDs on both axes: %1").arg(ids.join(QStringLiteral(", "))));
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
    (void)scene_->setRepresentation(*id, object->representation);
    (void)scene_->setColorScheme(*id, object->colorScheme);
    (void)scene_->setObjectOpacity(*id, object->opacity);
    if (report) *report = checks.join('\n');
    return passed;
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
    dispatcher_->selectAtom(*active, atom);
    const auto before = viewport_->grabFramebuffer();
    slider->setValue(3);
    const auto after = viewport_->grabFramebuffer();
    const auto selected = scene_->selectedAtom();
    const bool seek = frame->value() == 3 && selected && selected->id == atom && selected->position.x == 2.0F;
    const bool rendered = !before.isNull() && before.size() == after.size() && before != after;

    const auto script = juliaRuntime_.evaluate(QStringLiteral(
        "using JlMol\n"
        "mol = active_structure()\n"
        "@assert trajectory_info(mol) == (frames=3, frame=3, time_ps=nothing, cell=nothing)\n"
        "@assert trajectory_frame(mol, 1)[1,1] == 0\n"
        "@assert trajectory_info(mol).frame == 3\n"
        "@assert trajectory_rmsd(mol; align=false) ≈ [0, 1, 2]\n"
        "@assert maximum(trajectory_rmsd(mol)) < 1e-6\n"
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
        *report = QStringLiteral("Timeline seek and selection: %1\nFramebuffer update: %2\n"
                                 "Julia frame access and RMSD: %3\nTimer playback to final frame: %4")
                      .arg(seek ? QStringLiteral("passed") : QStringLiteral("failed"),
                           rendered ? QStringLiteral("passed") : QStringLiteral("failed"),
                           bridge && reset ? QStringLiteral("passed") : script,
                           playback ? QStringLiteral("passed") : QStringLiteral("failed"));
    }
    return seek && rendered && bridge && reset && playback;
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

void MainWindow::refreshSceneUi() {
    const QSignalBlocker blocker(objectTree_);
    objectTree_->clear();
    const auto objects = scene_->objects();
    const auto activeId = scene_->activeObjectId();
    if (objects.empty()) {
        atomCountLabel_->setText(QStringLiteral("No structure loaded"));
        refreshFrameUi();
        return;
    }
    std::size_t atomTotal = 0;
    std::size_t bondTotal = 0;
    for (const auto& object : objects) {
        if (!object.molecule) continue;
        const auto& molecule = *object.molecule;
        atomTotal += molecule.atoms().size();
        bondTotal += molecule.bonds().size();
        auto* root = new QTreeWidgetItem(objectTree_, {QString::fromStdString(molecule.name())});
        root->setData(0, objectIdRole, QVariant::fromValue<qulonglong>(object.id));
        root->setData(0, itemKindRole, objectItemKind);
        root->setFlags(root->flags() | Qt::ItemIsUserCheckable);
        root->setCheckState(0, object.visible ? Qt::Checked : Qt::Unchecked);
        root->setToolTip(0, QStringLiteral("%1\nOpacity: %2%")
                               .arg(QString::fromStdString(molecule.sourcePath().string()))
                               .arg(object.opacity * 100.0, 0, 'g', 4));
        root->setExpanded(objects.size() == 1);
        if (activeId == object.id) objectTree_->setCurrentItem(root);
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
    analysisMapScroll_->hide();
    if (const auto molecule = scene_->molecule()) {
        analysisSummary_->setText(QStringLiteral("<h3>%1</h3><p>Radius of gyration: <b>%2 Å</b></p>"
                                                "<p>Shift-click 2, 3, or 4 atoms for distance, angle, or dihedral.</p>")
                                      .arg(QString::fromStdString(molecule->name()))
                                      .arg(analysis::radiusOfGyration(*molecule), 0, 'f', 3));
    } else {
        analysisSummary_->setText(QStringLiteral("Open a structure to calculate geometric properties."));
    }
    updateInspector();
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
        if (analysisDock_->isVisible() && !measurement.isEmpty()) analysisSummary_->setText(measurement);
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

