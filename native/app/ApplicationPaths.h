#pragma once

#include <QString>

namespace jlmol {

struct ApplicationPaths {
    QString project;
    QString coreLibrary;
    QString juliaBin;
    bool bundled;
};

[[nodiscard]] ApplicationPaths applicationPaths();

} // namespace jlmol
