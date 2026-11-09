#pragma once

#include <QVector3D>

namespace jlmol {

struct RenderVertex {
    QVector3D position;
    QVector3D color;
    float radius{};
    float selected{};
    float scale{1.0F};
};

struct BondVertex {
    QVector3D position;
    QVector3D color;
};

struct RibbonVertex {
    QVector3D position;
    QVector3D color;
    QVector3D normal;
};

} // namespace jlmol

