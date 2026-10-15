#pragma once

#include "core/MolecularScene.h"
#include "renderer/RenderTypes.h"

#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_4_1_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <memory>
#include <vector>

namespace jlmol {

class MolecularViewport final : public QOpenGLWidget, protected QOpenGLFunctions_4_1_Core {
    Q_OBJECT
public:
    explicit MolecularViewport(std::shared_ptr<MolecularScene> scene, QWidget* parent = nullptr);
    ~MolecularViewport() override;

    void fitToScene();
    void setOrthographic(bool orthographic);

signals:
    void atomPicked(std::uint64_t atomId);
    void cameraChanged();

public slots:
    void sceneChanged();

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void rebuildGeometry();
    void releaseGlResources();
    [[nodiscard]] QMatrix4x4 modelViewMatrix() const;
    [[nodiscard]] QMatrix4x4 projectionMatrix() const;
    void pickAtom(const QPoint& point);
    [[nodiscard]] QVector3D colorForAtom(const Atom& atom) const;

    std::shared_ptr<MolecularScene> scene_;
    std::unique_ptr<QOpenGLShaderProgram> atomProgram_;
    std::unique_ptr<QOpenGLShaderProgram> bondProgram_;
    QOpenGLBuffer atomBuffer_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer bondBuffer_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject atomVao_;
    QOpenGLVertexArrayObject bondVao_;
    std::vector<RenderVertex> atoms_;
    std::vector<QVector3D> bondVertices_;
    std::uint64_t uploadedGeneration_{~std::uint64_t{0}};
    QPoint lastMousePosition_;
    QPoint pressPosition_;
    float yaw_{-25.0F};
    float pitch_{18.0F};
    float distance_{30.0F};
    QVector3D pan_;
    QVector3D sceneCenter_;
    float sceneRadius_{10.0F};
    bool orthographic_{};
};

} // namespace jlmol

