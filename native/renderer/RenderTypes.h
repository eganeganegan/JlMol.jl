#pragma once

#include <QVector3D>

namespace jlmol {

struct RenderVertex {
    QVector3D position;
    QVector3D color;
    float radius{};
    float selected{};
};

} // namespace jlmol

