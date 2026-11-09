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
    parser.addOption(smokeTest);
    parser.addOption(integrationTest);
    parser.addOption(editorTest);
    parser.addOption(graphicsTest);
    parser.process(app);

    jlmol::MainWindow window;
    const auto arguments = parser.positionalArguments();
    if (!arguments.isEmpty()) window.openStructure(arguments.first());
    window.show();
    if (parser.isSet(graphicsTest)) {
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
