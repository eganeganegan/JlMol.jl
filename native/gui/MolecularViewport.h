#pragma once

#include "core/MolecularScene.h"
#include "renderer/RenderTypes.h"

#include <QElapsedTimer>
#include <QColor>
#include <QImage>
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

class QPainter;
class QXmlStreamWriter;

namespace jlmol {

enum class ViewPreset { Front, Back, Left, Right, Top, Bottom };
enum class LabelMode { None, SelectedAtoms, SelectedResidues };
enum class LightingPreset { Standard, Soft, Flat };

class MolecularViewport final : public QOpenGLWidget, protected QOpenGLFunctions_4_1_Core {
    Q_OBJECT
public:
    struct CameraState {
        QQuaternion orientation;
        QVector3D pan;
        QVector3D center;
        float distance;
        float radius;
        bool orthographic;
        LabelMode labels;
        QColor background{9, 12, 17};
        LightingPreset lighting{LightingPreset::Standard};
        bool operator==(const CameraState&) const = default;
    };

    struct RedrawTiming {
        double geometryMs{};
        double uploadMs{};
        double totalMs{};
        std::size_t uploadBytes{};
    };

    explicit MolecularViewport(std::shared_ptr<MolecularScene> scene, QWidget* parent = nullptr);
    ~MolecularViewport() override;

    void fitToScene();
    void setOrthographic(bool orthographic);
    void setViewPreset(ViewPreset preset);
    void setLabelMode(LabelMode mode);
    void setBackground(const QColor& color);
    void setLighting(LightingPreset preset);
    [[nodiscard]] CameraState cameraState() const;
    void restoreCamera(const CameraState& state);
    [[nodiscard]] QImage renderImage(QSize pixels, bool transparentBackground, bool labels, QString& error,
                                     const MolecularObject* frame = nullptr);
    [[nodiscard]] static bool supportsImageSize(QSize pixels);
    void writeSvgLabels(QXmlStreamWriter& xml, QSize pixels) const;
    [[nodiscard]] RedrawTiming measureRedraw();

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

    struct ProjectedLabel {
        QPointF position;
        QString text;
    };

    void rebuildGeometry(const MolecularObject* frame = nullptr);
    bool updateExistingGeometry(const std::vector<MolecularObject>& objects, std::optional<ObjectId> selectedObject,
                                const std::vector<std::uint64_t>& selectedAtoms, QElapsedTimer& elapsed);
    void startUploadTiming(QElapsedTimer& elapsed, std::size_t bytes);
    void finishUploadTiming(const QElapsedTimer& elapsed);
    void releaseGlResources();
    bool renderGeometry(const QMatrix4x4& view, const QMatrix4x4& mvp, QSize pixels, float styleScale, GLuint target,
                        const QColor& background, std::unique_ptr<QOpenGLFramebufferObject>& transparency, bool exporting);
    void drawGeometry(const QMatrix4x4& view, const QMatrix4x4& mvp, QSize pixels, float styleScale, int pass,
                      bool opaqueOnly = true);
    [[nodiscard]] static std::unique_ptr<QOpenGLFramebufferObject> makeTransparencyBuffer(QSize pixels);
    bool drawTransparentScene(const QMatrix4x4& view, const QMatrix4x4& mvp, QSize pixels, float styleScale, GLuint target,
                              QOpenGLFramebufferObject& buffer);
    [[nodiscard]] QMatrix4x4 modelViewMatrix() const;
    [[nodiscard]] QMatrix4x4 projectionMatrix(QSize pixels) const;
    void pickAtom(const QPoint& point);
    [[nodiscard]] std::vector<ProjectedLabel> projectedLabels(const QMatrix4x4& mvp, QSize pixels,
                                                              const MolecularObject* frame) const;
    [[nodiscard]] QFont labelFont() const;
    void drawLabels(QPainter& painter, const QMatrix4x4& mvp, QSize pixels, float scale,
                    const MolecularObject* frame = nullptr);
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
    std::vector<MolecularObject> renderedObjects_;
    std::optional<ObjectId> renderedSelectionObject_;
    std::vector<std::uint64_t> renderedSelection_;
    std::uint64_t uploadedGeneration_{~std::uint64_t{0}};
    RedrawTiming* redrawTiming_{};
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
    float maxLineWidth_{1.0F};
    bool orthographic_{};
    LabelMode labelMode_{LabelMode::None};
    QColor background_{9, 12, 17};
    LightingPreset lighting_{LightingPreset::Standard};
};

} // namespace jlmol

