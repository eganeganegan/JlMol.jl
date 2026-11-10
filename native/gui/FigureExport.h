#pragma once

#include <QImage>
#include <QString>
#include <functional>

namespace jlmol {

class MolecularViewport;

struct FigureOptions {
    QSize pixels;
    bool transparent;
    bool labels;
    bool svg{};
};

bool saveFigure(MolecularViewport& viewport, const QString& path, const FigureOptions& options, QString& error);
// Returning a null image cancels the write and keeps any previous destination intact.
bool saveAnimatedPng(const QString& path, QSize pixels, int frameCount, int fps,
                     const std::function<QImage(int)>& renderFrame, QString& error);

} // namespace jlmol
