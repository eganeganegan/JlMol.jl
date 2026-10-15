#include "gui/MolecularViewport.h"

#include <QMouseEvent>
#include <QOpenGLContext>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace jlmol {
namespace {

constexpr auto atomVertexShader = R"GLSL(
#version 410 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in float radius;
layout(location = 3) in float selected;
uniform mat4 mvp;
uniform mat4 view;
uniform float viewportHeight;
uniform float scale;
out vec3 vertexColor;
out float isSelected;
void main() {
    vec4 eye = view * vec4(position, 1.0);
    gl_Position = mvp * vec4(position, 1.0);
    gl_PointSize = clamp(radius * scale * viewportHeight / max(1.0, -eye.z), 2.0, 180.0);
    vertexColor = color;
    isSelected = selected;
}
)GLSL";

constexpr auto atomFragmentShader = R"GLSL(
#version 410 core
in vec3 vertexColor;
in float isSelected;
out vec4 fragColor;
void main() {
    vec2 p = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(p, p);
    if (r2 > 1.0) discard;
    vec3 normal = vec3(p.x, -p.y, sqrt(max(0.0, 1.0 - r2)));
    float diffuse = 0.30 + 0.70 * max(0.0, dot(normalize(vec3(-0.35, 0.5, 1.0)), normal));
    vec3 base = mix(vertexColor, vec3(1.0, 0.72, 0.08), isSelected);
    float rim = isSelected * smoothstep(0.58, 0.95, sqrt(r2));
    fragColor = vec4(mix(base * diffuse, vec3(1.0, 0.9, 0.2), rim), 1.0);
}
)GLSL";

constexpr auto bondVertexShader = R"GLSL(
#version 410 core
layout(location = 0) in vec3 position;
uniform mat4 mvp;
void main() { gl_Position = mvp * vec4(position, 1.0); }
)GLSL";

constexpr auto bondFragmentShader = R"GLSL(
#version 410 core
out vec4 fragColor;
void main() { fragColor = vec4(0.52, 0.57, 0.64, 1.0); }
)GLSL";

std::unique_ptr<QOpenGLShaderProgram> makeProgram(const char* vertex, const char* fragment) {
    auto program = std::make_unique<QOpenGLShaderProgram>();
    if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex) ||
        !program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) || !program->link()) {
        throw std::runtime_error(program->log().toStdString());
    }
    return program;
}

QVector3D chainColor(const std::string& chain) {
    const auto hash = std::hash<std::string>{}(chain);
    return QVector3D(0.25F + static_cast<float>(hash & 255U) / 510.0F,
                     0.25F + static_cast<float>((hash >> 8U) & 255U) / 510.0F,
                     0.25F + static_cast<float>((hash >> 16U) & 255U) / 510.0F);
}

} // namespace

MolecularViewport::MolecularViewport(std::shared_ptr<MolecularScene> scene, QWidget* parent)
    : QOpenGLWidget(parent), scene_(std::move(scene)) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(480, 320);
}

MolecularViewport::~MolecularViewport() {
    if (context()) {
        makeCurrent();
        releaseGlResources();
        doneCurrent();
    }
}

void MolecularViewport::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_MULTISAMPLE);
    glClearColor(0.035F, 0.047F, 0.066F, 1.0F);
    atomProgram_ = makeProgram(atomVertexShader, atomFragmentShader);
    bondProgram_ = makeProgram(bondVertexShader, bondFragmentShader);
    atomBuffer_.create();
    bondBuffer_.create();
    atomVao_.create();
    bondVao_.create();
    rebuildGeometry();
}

void MolecularViewport::resizeGL(int, int) {}

void MolecularViewport::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (uploadedGeneration_ != scene_->generation()) {
        rebuildGeometry();
    }
    if (!atomProgram_) {
        return;
    }
    const auto view = modelViewMatrix();
    const auto projection = projectionMatrix();
    const auto mvp = projection * view;
    const auto representation = scene_->representation();

    if (!bondVertices_.empty() && representation != Representation::SpaceFilling && representation != Representation::Points) {
        bondProgram_->bind();
        bondProgram_->setUniformValue("mvp", mvp);
        bondVao_.bind();
        glLineWidth(representation == Representation::Lines ? 2.0F : 4.0F);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(bondVertices_.size()));
        bondVao_.release();
        bondProgram_->release();
    }

    if (representation != Representation::Lines && !atoms_.empty()) {
        atomProgram_->bind();
        atomProgram_->setUniformValue("mvp", mvp);
        atomProgram_->setUniformValue("view", view);
        atomProgram_->setUniformValue("viewportHeight", static_cast<float>(height() * devicePixelRatioF()));
        const float scale = representation == Representation::SpaceFilling ? 2.15F :
                            representation == Representation::Points ? 0.28F : 0.72F;
        atomProgram_->setUniformValue("scale", scale);
        atomVao_.bind();
        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(atoms_.size()));
        atomVao_.release();
        atomProgram_->release();
    }
}

void MolecularViewport::sceneChanged() {
    uploadedGeneration_ = ~std::uint64_t{0};
    update();
}

void MolecularViewport::rebuildGeometry() {
    if (!context()) return;
    const auto molecule = scene_->molecule();
    atoms_.clear();
    bondVertices_.clear();
    if (molecule) {
        const auto selection = scene_->selectedAtomId();
        atoms_.reserve(molecule->atoms().size());
        for (const auto& atom : molecule->atoms()) {
            atoms_.push_back(RenderVertex{QVector3D(atom.position.x, atom.position.y, atom.position.z), colorForAtom(atom),
                                          elementRadius(atom.element), selection == atom.id ? 1.0F : 0.0F});
        }
        bondVertices_.reserve(molecule->bonds().size() * 2U);
        for (const auto& bond : molecule->bonds()) {
            const auto& a = molecule->atoms()[bond.first].position;
            const auto& b = molecule->atoms()[bond.second].position;
            bondVertices_.emplace_back(a.x, a.y, a.z);
            bondVertices_.emplace_back(b.x, b.y, b.z);
        }
    }

    atomVao_.bind();
    atomBuffer_.bind();
    atomBuffer_.allocate(atoms_.data(), static_cast<int>(atoms_.size() * sizeof(RenderVertex)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void*>(offsetof(RenderVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void*>(offsetof(RenderVertex, color)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void*>(offsetof(RenderVertex, radius)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void*>(offsetof(RenderVertex, selected)));
    atomBuffer_.release();
    atomVao_.release();

    bondVao_.bind();
    bondBuffer_.bind();
    bondBuffer_.allocate(bondVertices_.data(), static_cast<int>(bondVertices_.size() * sizeof(QVector3D)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(QVector3D), nullptr);
    bondBuffer_.release();
    bondVao_.release();
    uploadedGeneration_ = scene_->generation();
}

void MolecularViewport::releaseGlResources() {
    atomVao_.destroy();
    bondVao_.destroy();
    atomBuffer_.destroy();
    bondBuffer_.destroy();
    atomProgram_.reset();
    bondProgram_.reset();
}

QMatrix4x4 MolecularViewport::modelViewMatrix() const {
    QMatrix4x4 view;
    view.translate(pan_);
    view.translate(0.0F, 0.0F, -distance_);
    view.rotate(pitch_, 1.0F, 0.0F, 0.0F);
    view.rotate(yaw_, 0.0F, 1.0F, 0.0F);
    view.translate(-sceneCenter_);
    return view;
}

QMatrix4x4 MolecularViewport::projectionMatrix() const {
    QMatrix4x4 projection;
    const float aspect = static_cast<float>(std::max(1, width())) / static_cast<float>(std::max(1, height()));
    if (orthographic_) {
        const float half = std::max(1.0F, distance_ * 0.45F);
        projection.ortho(-half * aspect, half * aspect, -half, half, 0.1F, 2000.0F);
    } else {
        projection.perspective(38.0F, aspect, 0.1F, 2000.0F);
    }
    return projection;
}

void MolecularViewport::fitToScene() {
    const auto molecule = scene_->molecule();
    if (molecule) {
        const auto center = molecule->center();
        sceneCenter_ = QVector3D(center.x, center.y, center.z);
        sceneRadius_ = molecule->radius();
        distance_ = std::max(6.0F, sceneRadius_ * 3.0F);
        pan_ = {};
    }
    update();
}

void MolecularViewport::setOrthographic(bool orthographic) {
    orthographic_ = orthographic;
    update();
}

void MolecularViewport::mousePressEvent(QMouseEvent* event) {
    lastMousePosition_ = event->position().toPoint();
    pressPosition_ = lastMousePosition_;
}

void MolecularViewport::mouseMoveEvent(QMouseEvent* event) {
    const QPoint current = event->position().toPoint();
    const QPoint delta = current - lastMousePosition_;
    lastMousePosition_ = current;
    if (event->buttons() & Qt::LeftButton) {
        yaw_ += delta.x() * 0.45F;
        pitch_ = std::clamp(pitch_ + delta.y() * 0.45F, -89.0F, 89.0F);
    } else if (event->buttons() & (Qt::MiddleButton | Qt::RightButton)) {
        const float scale = distance_ / std::max(200.0F, static_cast<float>(height())) * 1.2F;
        pan_ += QVector3D(delta.x() * scale, -delta.y() * scale, 0.0F);
    }
    emit cameraChanged();
    update();
}

void MolecularViewport::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && (event->position().toPoint() - pressPosition_).manhattanLength() < 5) {
        pickAtom(event->position().toPoint());
    }
}

void MolecularViewport::wheelEvent(QWheelEvent* event) {
    const float exponent = std::clamp(-event->angleDelta().y() / 1200.0F, -0.55F, 0.55F);
    distance_ = std::clamp(distance_ * std::exp(exponent), 1.5F, 1500.0F);
    emit cameraChanged();
    update();
}

void MolecularViewport::pickAtom(const QPoint& point) {
    const auto molecule = scene_->molecule();
    if (!molecule) return;
    const auto mvp = projectionMatrix() * modelViewMatrix();
    float bestDistance = 14.0F * 14.0F;
    float bestDepth = std::numeric_limits<float>::max();
    std::optional<std::uint64_t> best;
    for (const auto& atom : molecule->atoms()) {
        const QVector4D clip = mvp * QVector4D(atom.position.x, atom.position.y, atom.position.z, 1.0F);
        if (clip.w() <= 0.0F) continue;
        const QVector3D ndc = clip.toVector3DAffine();
        const float x = (ndc.x() * 0.5F + 0.5F) * width();
        const float y = (1.0F - (ndc.y() * 0.5F + 0.5F)) * height();
        const float dx = x - point.x();
        const float dy = y - point.y();
        const float d2 = dx * dx + dy * dy;
        if (d2 <= bestDistance && ndc.z() < bestDepth) {
            bestDistance = d2;
            bestDepth = ndc.z();
            best = atom.id;
        }
    }
    if (best) emit atomPicked(*best);
}

QVector3D MolecularViewport::colorForAtom(const Atom& atom) const {
    switch (scene_->colorScheme()) {
    case ColorScheme::Element: {
        const auto c = elementColor(atom.element);
        return QVector3D(c[0], c[1], c[2]);
    }
    case ColorScheme::Chain: return chainColor(atom.chainId);
    case ColorScheme::Residue: return chainColor(atom.residueName + std::to_string(atom.residueId));
    }
    return QVector3D(0.8F, 0.8F, 0.8F);
}

} // namespace jlmol
