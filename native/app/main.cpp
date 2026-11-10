#include "gui/MainWindow.h"
#include "bridge/JlMolAPI.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QScopeGuard>
#include <QSettings>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTimer>
#include <string_view>

namespace {

int runScientificReference() {
    jlmol::MolecularScene scene;
    jlmol_set_active_scene(&scene);
    const auto clearScene = qScopeGuard([] { jlmol_set_active_scene(nullptr); });
    jlmol::JuliaRuntime runtime;
    QString error;
    if (!runtime.initialize(&error)) {
        qCritical().noquote() << error;
        return 1;
    }
    const auto result = runtime.evaluateSeries(QStringLiteral(
        "include(joinpath(ENV[\"JULIA_PROJECT\"], \"test\", \"scientific_reference.jl\"))\nFloat64[1.0]"));
    if (!result.error.isEmpty()) qCritical().noquote() << result.error;
    return result.error.isEmpty() && result.values.size() == 1 && result.values.front() == 1.0 ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[]) {
    bool scientificOnly = false;
    for (int i = 1; i < argc && std::string_view(argv[i]) != "--"; ++i) {
        if (std::string_view(argv[i]) == "--scientific-test") scientificOnly = true;
    }
    QSurfaceFormat format;
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    std::unique_ptr<QCoreApplication> application;
    if (scientificOnly) application = std::make_unique<QCoreApplication>(argc, argv);
    else application = std::make_unique<QApplication>(argc, argv);
    auto& app = *application;
    QCoreApplication::setOrganizationName(QStringLiteral("JlMol"));
    QCoreApplication::setApplicationName(QStringLiteral("JlMol"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("A Julia-Powered Molecular Visualization Workstation"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("structure"), QStringLiteral("Molecular structure to open"), QStringLiteral("[structure]"));
    QCommandLineOption smokeTest(QStringLiteral("smoke-test"), QStringLiteral("Initialize the GUI and Julia, then exit automatically."));
    QCommandLineOption integrationTest(QStringLiteral("integration-test"), QStringLiteral("Run the native/Julia shared-scene integration test and exit."));
    QCommandLineOption editorTest(QStringLiteral("editor-test"), QStringLiteral("Run the Julia script editor integration test and exit."));
    QCommandLineOption graphicsTest(QStringLiteral("graphics-test"), QStringLiteral("Check rendered transparency and protein colors, then exit."));
    QCommandLineOption trajectoryTest(QStringLiteral("trajectory-test"),
        QStringLiteral("Test frame access and playback using the supplied trajectory fixture."), QStringLiteral("path"));
    QCommandLineOption linkedAnalysisTest(QStringLiteral("linked-analysis-test"),
        QStringLiteral("Test linked trajectory analysis using the supplied fixture."), QStringLiteral("path"));
    QCommandLineOption pluginTest(QStringLiteral("plugin-test"),
        QStringLiteral("Test GUI commands, analysis, and coloring using the Structure Tools plugin."), QStringLiteral("path"));
    QCommandLineOption workspaceTest(QStringLiteral("workspace-test"),
        QStringLiteral("Test saving and reopening a workspace using the water trajectory fixture."), QStringLiteral("path"));
    QCommandLineOption exportTest(QStringLiteral("export-test"),
        QStringLiteral("Test PNG, SVG, and animation export using the water trajectory fixture."), QStringLiteral("path"));
    QCommandLineOption benchmark(QStringLiteral("benchmark"),
        QStringLiteral("Measure native rendering and trajectory operations at 1k, 10k, and 100k atoms."));
    QCommandLineOption scientificTest(QStringLiteral("scientific-test"),
        QStringLiteral("Run independent scientific reference checks without opening a window."));
    parser.addOption(smokeTest);
    parser.addOption(integrationTest);
    parser.addOption(editorTest);
    parser.addOption(graphicsTest);
    parser.addOption(trajectoryTest);
    parser.addOption(linkedAnalysisTest);
    parser.addOption(pluginTest);
    parser.addOption(workspaceTest);
    parser.addOption(exportTest);
    parser.addOption(benchmark);
    parser.addOption(scientificTest);
    parser.process(app);

    if (parser.isSet(scientificTest)) return runScientificReference();

    std::unique_ptr<QTemporaryDir> benchmarkSettings;
    if (parser.isSet(benchmark)) {
        benchmarkSettings = std::make_unique<QTemporaryDir>();
        if (!benchmarkSettings->isValid()) return 1;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, benchmarkSettings->path());
    }
    jlmol::MainWindow window;
    const auto arguments = parser.positionalArguments();
    if (parser.isSet(benchmark)) window.resize(1200, 820);
    else if (!arguments.isEmpty()) window.openStructure(arguments.first());
    window.show();
    if (parser.isSet(benchmark)) {
        QTimer::singleShot(0, &app, [&app, &window] {
            QString report;
            const bool passed = window.runPerformanceBenchmark(&report);
            qInfo().noquote() << report;
            app.exit(passed ? 0 : 1);
        });
    } else if (parser.isSet(exportTest)) {
        QTimer::singleShot(0, &app, [&app, &window, &parser, exportTest] {
            QString report;
            const bool passed = window.runExportSelfTest(parser.value(exportTest), &report);
            qInfo().noquote() << report;
            app.exit(passed ? 0 : 1);
        });
    } else if (parser.isSet(workspaceTest)) {
        QTimer::singleShot(0, &app, [&app, &window, &parser, workspaceTest] {
            QString report;
            const bool passed = window.runWorkspaceSelfTest(parser.value(workspaceTest), &report);
            qInfo().noquote() << report;
            app.exit(passed ? 0 : 1);
        });
    } else if (parser.isSet(pluginTest)) {
        QTimer::singleShot(0, &app, [&app, &window, &parser, pluginTest] {
            QString report;
            const bool passed = window.runPluginSelfTest(parser.value(pluginTest), &report);
            qInfo().noquote() << report;
            app.exit(passed ? 0 : 1);
        });
    } else if (parser.isSet(linkedAnalysisTest)) {
        QTimer::singleShot(0, &app, [&app, &window, &parser, linkedAnalysisTest] {
            QString report;
            const bool attached = window.attachTrajectory(parser.value(linkedAnalysisTest));
            const bool passed = attached && window.runLinkedAnalysisSelfTest(&report);
            qInfo().noquote() << (attached ? report : QStringLiteral("Could not attach the analysis trajectory fixture"));
            app.exit(passed ? 0 : 1);
        });
    } else if (parser.isSet(trajectoryTest)) {
        QTimer::singleShot(0, &app, [&app, &window, &parser, trajectoryTest] {
            QString report;
            const bool attached = window.attachTrajectory(parser.value(trajectoryTest));
            const bool passed = attached && window.runTrajectorySelfTest(&report);
            qInfo().noquote() << (attached ? report : QStringLiteral("Could not attach the trajectory fixture"));
            app.exit(passed ? 0 : 1);
        });
    } else if (parser.isSet(graphicsTest)) {
        QTimer::singleShot(0, &app, [&app, &window] {
            QString report;
            const bool passed = window.runGraphicsSelfTest(&report);
            qInfo().noquote() << report;
            app.exit(passed ? 0 : 1);
        });
    } else if (parser.isSet(editorTest)) {
        QTimer::singleShot(0, &app, [&app, &window] {
            QString report;
            const bool passed = window.runScriptEditorSelfTest(&report);
            qInfo().noquote() << report;
            app.exit(passed ? 0 : 1);
        });
    } else if (parser.isSet(integrationTest)) {
        QTimer::singleShot(0, &app, [&app, &window] {
            QString report;
            const bool passed = window.runIntegrationSelfTest(&report);
            qInfo().noquote() << report;
            app.exit(passed ? 0 : 1);
        });
    } else if (parser.isSet(smokeTest)) {
        QTimer::singleShot(250, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
