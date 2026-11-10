#include "gui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QSurfaceFormat>
#include <QTimer>

int main(int argc, char* argv[]) {
    QSurfaceFormat format;
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
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
    parser.addOption(smokeTest);
    parser.addOption(integrationTest);
    parser.addOption(editorTest);
    parser.addOption(graphicsTest);
    parser.addOption(trajectoryTest);
    parser.addOption(linkedAnalysisTest);
    parser.process(app);

    jlmol::MainWindow window;
    const auto arguments = parser.positionalArguments();
    if (!arguments.isEmpty()) window.openStructure(arguments.first());
    window.show();
    if (parser.isSet(linkedAnalysisTest)) {
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
