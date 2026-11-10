#include "app/ApplicationPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace jlmol {

ApplicationPaths applicationPaths() {
    const QDir executable(QCoreApplication::applicationDirPath());
    const auto fromExecutable = [&executable](const QString& path) { return QDir::cleanPath(executable.absoluteFilePath(path)); };
#if defined(Q_OS_MACOS)
    const auto project = fromExecutable(QStringLiteral("../Resources/jlmol"));
    const auto libraries = fromExecutable(QStringLiteral("../Frameworks"));
    const auto julia = fromExecutable(QStringLiteral("../Resources/Julia/bin"));
    const auto marker = julia;
#elif defined(Q_OS_WIN)
    const auto project = fromExecutable(QStringLiteral("../share/jlmol"));
    const auto libraries = executable.absolutePath();
    const auto julia = libraries;
    const auto marker = executable.filePath(QStringLiteral("julia.exe"));
#else
    const auto project = fromExecutable(QStringLiteral("../share/jlmol"));
    const auto libraries = fromExecutable(QStringLiteral("../lib"));
    const auto julia = fromExecutable(QStringLiteral("../lib/jlmol/Julia/bin"));
    const auto marker = julia;
#endif
    if (QFileInfo::exists(project) || QFileInfo::exists(marker)) {
        return {project, QDir(libraries).filePath(QStringLiteral(JLMOL_CORE_LIBRARY_NAME)), julia, true};
    }
#ifdef JLMOL_HAS_JULIA
    const auto developerJulia = QStringLiteral(JLMOL_JULIA_BINDIR);
#else
    const QString developerJulia;
#endif
    return {QStringLiteral(JLMOL_SOURCE_DIR), QStringLiteral(JLMOL_CORE_LIBRARY_PATH), developerJulia, false};
}

} // namespace jlmol
