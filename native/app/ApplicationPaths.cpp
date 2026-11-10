#include "app/ApplicationPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace jlmol {

ApplicationPaths applicationPaths() {
#ifdef Q_OS_MACOS
    const QDir executable(QCoreApplication::applicationDirPath());
    const auto frameworks = QDir::cleanPath(executable.absoluteFilePath(QStringLiteral("../Frameworks")));
    const auto project = QDir::cleanPath(executable.absoluteFilePath(QStringLiteral("../Resources/jlmol")));
    const auto core = QDir(frameworks).filePath(QStringLiteral(JLMOL_CORE_LIBRARY_NAME));
    const auto julia = QDir::cleanPath(executable.absoluteFilePath(QStringLiteral("../Resources/Julia/bin")));
    if (QFileInfo::exists(core) || QFileInfo::exists(project) || QFileInfo::exists(julia)) {
        return {project, core, julia, true};
    }
#endif
#ifdef JLMOL_HAS_JULIA
    const auto developerJulia = QStringLiteral(JLMOL_JULIA_BINDIR);
#else
    const QString developerJulia;
#endif
    return {QStringLiteral(JLMOL_SOURCE_DIR), QStringLiteral(JLMOL_CORE_LIBRARY_PATH), developerJulia, false};
}

} // namespace jlmol
