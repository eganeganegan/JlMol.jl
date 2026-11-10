#pragma once

#include "core/MolecularScene.h"
#include "renderer/RenderTypes.h"

#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_4_1_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPointF>
#include <QQuaternion>
#include <QTimer>
#include <memory>
#include <vector>

namespace jlmol {

enum class ViewPreset { Front, Back, Left, Right, Top, Bottom };
enum class LabelMode { None, SelectedAtoms, SelectedResidues };

class MolecularViewport final : public QOpenGLWidget, protected QOpenGLFunctions_4_1_Core {
    Q_OBJECT
public:
    explicit MolecularViewport(std::shared_ptr<MolecularScene> scene, QWidget* parent = nullptr);
    ~MolecularViewport() override;

    void fitToScene();
    void setOrthographic(bool orthographic);
    void setViewPreset(ViewPreset preset);
    void setLabelMode(LabelMode mode);

signals:
    void atomPicked(ObjectId objectId, std::uint64_t atomId, bool extendSelection);
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
    struct DrawRange {
        GLint first{};
        GLsizei count{};
    };
    struct DrawBatch {
        float opacity{1.0F};
        DrawRange atoms;
        DrawRange bonds;
        DrawRange ribbons;
    };

    void rebuildGeometry();
    void releaseGlResources();
    void drawGeometry(const QMatrix4x4& view, const QMatrix4x4& mvp, int pass, bool opaqueOnly = true);
    bool drawTransparentScene(const QMatrix4x4& view, const QMatrix4x4& mvp);
    [[nodiscard]] QMatrix4x4 modelViewMatrix() const;
    [[nodiscard]] QMatrix4x4 projectionMatrix() const;
    void pickAtom(const QPoint& point);
    void drawLabels(const QMatrix4x4& mvp);
    [[nodiscard]] static QVector3D colorForAtom(const Atom& atom, ColorScheme scheme);

    std::shared_ptr<MolecularScene> scene_;
    std::unique_ptr<QOpenGLShaderProgram> atomProgram_;
    std::unique_ptr<QOpenGLShaderProgram> bondProgram_;
    std::unique_ptr<QOpenGLShaderProgram> ribbonProgram_;
    std::unique_ptr<QOpenGLShaderProgram> compositeProgram_;
    std::unique_ptr<QOpenGLFramebufferObject> transparencyBuffer_;
    QOpenGLBuffer atomBuffer_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer bondBuffer_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer ribbonBuffer_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer ribbonIndexBuffer_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject atomVao_;
    QOpenGLVertexArrayObject bondVao_;
    QOpenGLVertexArrayObject ribbonVao_;
    QOpenGLVertexArrayObject compositeVao_;
    std::vector<RenderVertex> atoms_;
    std::vector<BondVertex> bondVertices_;
    std::vector<RibbonVertex> ribbonVertices_;
    std::vector<std::uint32_t> ribbonIndices_;
    std::vector<DrawBatch> batches_;
    std::uint64_t uploadedGeneration_{~std::uint64_t{0}};
    QPointF lastMousePosition_;
    QPointF pressPosition_;
    bool dragging_{};
    QQuaternion orientation_;
    float distance_{30.0F};
    float zoomTarget_{30.0F};
    QTimer zoomTimer_;
    QElapsedTimer zoomClock_;
    QVector3D pan_;
    QVector3D sceneCenter_;
    float sceneRadius_{10.0F};
    float lineWidth_{1.0F};
    bool orthographic_{};
    LabelMode labelMode_{LabelMode::None};
};

} // namespace jlmol

