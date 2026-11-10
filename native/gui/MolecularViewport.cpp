#include "gui/MolecularViewport.h"
#include "renderer/CartoonGeometry.h"

#include <QApplication>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <unordered_set>

namespace jlmol {
namespace {

constexpr auto geometryFragmentPrelude = R"GLSL(#version 410 core
uniform float opacity;
uniform int renderPass;
vec4 encodeFragment(vec3 rgb) {
    if (renderPass == 1) {
        // Weighted blended transparency: favor nearer fragments in perspective.
        float weight = 1.0 + clamp(200.0 * gl_FragCoord.w, 0.0, 8.0);
        return vec4(rgb * opacity, opacity) * weight;
    }
    if (renderPass == 2) return vec4(0.0, 0.0, 0.0, opacity);
    return vec4(rgb, 1.0);
}
)GLSL";

constexpr auto compositeVertexShader = R"GLSL(
#version 410 core
void main() {
    vec2 corner = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

constexpr auto compositeFragmentShader = R"GLSL(
#version 410 core
uniform sampler2D opaqueTexture;
uniform sampler2D accumulationTexture;
uniform sampler2D revealageTexture;
out vec4 fragColor;
void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec3 opaque = texelFetch(opaqueTexture, pixel, 0).rgb;
    vec4 accumulation = texelFetch(accumulationTexture, pixel, 0);
    float revealage = texelFetch(revealageTexture, pixel, 0).r;
    vec3 transparent = accumulation.rgb / max(accumulation.a, 1.0e-12);
    fragColor = vec4(mix(transparent, opaque, revealage), 1.0);
}
)GLSL";

constexpr auto atomVertexShader = R"GLSL(
#version 410 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in float radius;
layout(location = 3) in float selected;
layout(location = 4) in float atomScale;
uniform mat4 mvp;
uniform mat4 view;
uniform float viewportHeight;
out vec3 vertexColor;
out float isSelected;
void main() {
    vec4 eye = view * vec4(position, 1.0);
    gl_Position = mvp * vec4(position, 1.0);
    gl_PointSize = clamp(radius * atomScale * viewportHeight / max(1.0, -eye.z), 2.0, 180.0);
    vertexColor = color;
    isSelected = selected;
}
)GLSL";

constexpr auto atomFragmentShader = R"GLSL(
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
    fragColor = encodeFragment(mix(base * diffuse, vec3(1.0, 0.9, 0.2), rim));
}
)GLSL";

constexpr auto bondVertexShader = R"GLSL(
#version 410 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
uniform mat4 mvp;
out vec3 vertexColor;
void main() {
    gl_Position = mvp * vec4(position, 1.0);
    vertexColor = color;
}
)GLSL";

constexpr auto bondFragmentShader = R"GLSL(
in vec3 vertexColor;
out vec4 fragColor;
void main() { fragColor = encodeFragment(vertexColor); }
)GLSL";

constexpr auto ribbonVertexShader = R"GLSL(
#version 410 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 normal;
uniform mat4 mvp;
uniform mat3 normalMatrix;
out vec3 vertexColor;
out vec3 eyeNormal;
void main() {
    gl_Position = mvp * vec4(position, 1.0);
    vertexColor = color;
    eyeNormal = normalize(normalMatrix * normal);
}
)GLSL";

constexpr auto ribbonFragmentShader = R"GLSL(
in vec3 vertexColor;
in vec3 eyeNormal;
out vec4 fragColor;
void main() {
    vec3 n = normalize(eyeNormal);
    if (!gl_FrontFacing) n = -n;
    vec3 key = normalize(vec3(-0.45, 0.60, 1.0));
    float diffuse = max(0.0, dot(n, key));
    float fill = max(0.0, dot(n, normalize(vec3(0.7, -0.4, 0.55))));
    float highlight = diffuse * pow(max(0.0, dot(n, normalize(key + vec3(0, 0, 1)))), 25.0);
    vec3 lit = vertexColor * (0.34 + 0.57 * diffuse + 0.16 * fill);
    fragColor = encodeFragment(lit + 0.14 * highlight * mix(vertexColor, vec3(1.0), 0.45));
}
)GLSL";

std::unique_ptr<QOpenGLShaderProgram> makeProgram(const char* vertex, const QByteArray& fragment) {
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

QQuaternion orientationFor(float yaw, float pitch) {
    return QQuaternion::fromAxisAndAngle(1, 0, 0, pitch) * QQuaternion::fromAxisAndAngle(0, 1, 0, yaw);
}

} // namespace

MolecularViewport::MolecularViewport(std::shared_ptr<MolecularScene> scene, QWidget* parent)
    : QOpenGLWidget(parent), scene_(std::move(scene)), orientation_(orientationFor(-25.F, 18.F)) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(480, 320);
    zoomTimer_.setTimerType(Qt::PreciseTimer);
    zoomTimer_.setInterval(16);
    connect(&zoomTimer_, &QTimer::timeout, this, [this] {
        const auto elapsed = std::clamp(static_cast<float>(zoomClock_.nsecsElapsed()) / 1.0e9F, 0.001F, 0.05F);
        zoomClock_.restart();
        distance_ = std::lerp(distance_, zoomTarget_, 1.F - std::exp(-elapsed / 0.042F));
        if (std::abs(distance_ - zoomTarget_) <= zoomTarget_ * 0.0005F) {
            distance_ = zoomTarget_;
            zoomTimer_.stop();
        }
        emit cameraChanged();
        update();
    });
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
    GLfloat lineWidths[2]{1.0F, 1.0F};
    glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, lineWidths);
    lineWidth_ = std::clamp(3.0F, lineWidths[0], lineWidths[1]);
    glClearColor(0.035F, 0.047F, 0.066F, 1.0F);
    atomProgram_ = makeProgram(atomVertexShader, QByteArray(geometryFragmentPrelude) + atomFragmentShader);
    bondProgram_ = makeProgram(bondVertexShader, QByteArray(geometryFragmentPrelude) + bondFragmentShader);
    ribbonProgram_ = makeProgram(ribbonVertexShader, QByteArray(geometryFragmentPrelude) + ribbonFragmentShader);
    compositeProgram_ = makeProgram(compositeVertexShader, compositeFragmentShader);
    atomBuffer_.create();
    bondBuffer_.create();
    ribbonBuffer_.create();
    ribbonIndexBuffer_.create();
    atomVao_.create();
    bondVao_.create();
    ribbonVao_.create();
    compositeVao_.create();
    rebuildGeometry();
}

void MolecularViewport::resizeGL(int, int) {}

void MolecularViewport::paintGL() {
    if (uploadedGeneration_ != scene_->generation()) {
        rebuildGeometry();
    }
    if (!atomProgram_) {
        return;
    }
    const auto view = modelViewMatrix();
    const auto mvp = projectionMatrix() * view;
    const bool transparent = std::any_of(batches_.begin(), batches_.end(), [](const auto& batch) {
        return batch.opacity < 1.0F && (batch.atoms.count || batch.bonds.count || batch.ribbons.count);
    });
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    if (!transparent) transparencyBuffer_.reset();
    if (!transparent || !drawTransparentScene(view, mvp)) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawGeometry(view, mvp, 0, false);
    }
    drawLabels(mvp);
}

void MolecularViewport::drawGeometry(const QMatrix4x4& view, const QMatrix4x4& mvp, int pass, bool opaqueOnly) {
    const auto draw = [&](QOpenGLShaderProgram& program, GLenum mode, DrawRange DrawBatch::*range) {
        program.setUniformValue("renderPass", pass);
        for (const auto& batch : batches_) {
            if (pass != 0 ? batch.opacity >= 1.0F : opaqueOnly && batch.opacity < 1.0F) continue;
            const auto [first, count] = batch.*range;
            if (!count) continue;
            program.setUniformValue("opacity", batch.opacity);
            if (mode == GL_TRIANGLES) {
                const auto offset = static_cast<std::uintptr_t>(first) * sizeof(std::uint32_t);
                glDrawElements(mode, count, GL_UNSIGNED_INT, reinterpret_cast<const void*>(offset));
            } else {
                glDrawArrays(mode, first, count);
            }
        }
    };
    if (!ribbonVertices_.empty()) {
        ribbonProgram_->bind();
        ribbonProgram_->setUniformValue("mvp", mvp);
        ribbonProgram_->setUniformValue("normalMatrix", view.normalMatrix());
        ribbonVao_.bind();
        draw(*ribbonProgram_, GL_TRIANGLES, &DrawBatch::ribbons);
        ribbonVao_.release();
        ribbonProgram_->release();
    }
    if (!bondVertices_.empty()) {
        bondProgram_->bind();
        bondProgram_->setUniformValue("mvp", mvp);
        bondVao_.bind();
        glLineWidth(lineWidth_);
        draw(*bondProgram_, GL_LINES, &DrawBatch::bonds);
        bondVao_.release();
        bondProgram_->release();
    }

    if (!atoms_.empty()) {
        atomProgram_->bind();
        atomProgram_->setUniformValue("mvp", mvp);
        atomProgram_->setUniformValue("view", view);
        atomProgram_->setUniformValue("viewportHeight", static_cast<float>(height() * devicePixelRatioF()));
        atomVao_.bind();
        draw(*atomProgram_, GL_POINTS, &DrawBatch::atoms);
        atomVao_.release();
        atomProgram_->release();
    }
}

bool MolecularViewport::drawTransparentScene(const QMatrix4x4& view, const QMatrix4x4& mvp) {
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    const QSize pixels(viewport[2], viewport[3]);
    if (pixels.isEmpty()) return false;
    if (!transparencyBuffer_ || transparencyBuffer_->size() != pixels) {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::Depth);
        format.setInternalTextureFormat(GL_RGBA8);
        transparencyBuffer_ = std::make_unique<QOpenGLFramebufferObject>(pixels, format);
        if (transparencyBuffer_->isValid()) {
            transparencyBuffer_->addColorAttachment(pixels, GL_RGBA16F);
            transparencyBuffer_->addColorAttachment(pixels, GL_R16F);
        }
        if (!transparencyBuffer_->isValid() || transparencyBuffer_->textures().size() != 3) {
            qWarning("JlMol: transparency framebuffer unavailable; using opaque rendering.");
        }
    }
    const auto textures = transparencyBuffer_->textures();
    if (!transparencyBuffer_->isValid() || textures.size() != 3 || !transparencyBuffer_->bind()) {
        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        return false;
    }

    // The three color targets share the same opaque depth buffer.
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawGeometry(view, mvp, 0);

    constexpr GLfloat clear[4]{0, 0, 0, 0};
    constexpr GLfloat reveal[4]{1, 1, 1, 1};
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glDrawBuffer(GL_COLOR_ATTACHMENT1);
    glClearBufferfv(GL_COLOR, 0, clear);
    glBlendFunc(GL_ONE, GL_ONE);
    drawGeometry(view, mvp, 1);
    glDrawBuffer(GL_COLOR_ATTACHMENT2);
    glClearBufferfv(GL_COLOR, 0, reveal);
    glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
    drawGeometry(view, mvp, 2);

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    compositeProgram_->bind();
    compositeProgram_->setUniformValue("opaqueTexture", 0);
    compositeProgram_->setUniformValue("accumulationTexture", 1);
    compositeProgram_->setUniformValue("revealageTexture", 2);
    for (int unit = 0; unit < 3; ++unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, textures[unit]);
    }
    compositeVao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    compositeVao_.release();
    for (int unit = 2; unit >= 0; --unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    compositeProgram_->release();
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    return true;
}

void MolecularViewport::sceneChanged() {
    uploadedGeneration_ = ~std::uint64_t{0};
    update();
}

void MolecularViewport::rebuildGeometry() {
    if (!context()) return;
    atoms_.clear();
    bondVertices_.clear();
    ribbonVertices_.clear();
    ribbonIndices_.clear();
    batches_.clear();
    const auto selectedObject = scene_->selectedObjectId();
    const auto selectedAtomIds = scene_->selectedAtomIds();
    const std::unordered_set<std::uint64_t> selectedAtoms(selectedAtomIds.begin(), selectedAtomIds.end());
    for (const auto& object : scene_->objects()) {
        if (!object.visible || object.opacity <= 0.0 || !object.molecule) continue;
        DrawBatch batch;
        batch.opacity = static_cast<float>(object.opacity);
        batch.atoms.first = static_cast<GLint>(atoms_.size());
        batch.bonds.first = static_cast<GLint>(bondVertices_.size());
        batch.ribbons.first = static_cast<GLint>(ribbonIndices_.size());
        const auto& molecule = *object.molecule;
        if (object.representation == Representation::BallAndStick ||
            object.representation == Representation::SpaceFilling ||
            object.representation == Representation::Points) {
            const float scale = object.representation == Representation::SpaceFilling ? 2.15F :
                                object.representation == Representation::Points ? 0.28F : 0.72F;
            atoms_.reserve(atoms_.size() + molecule.atoms().size());
            for (const auto& atom : molecule.atoms()) {
                const bool selected = selectedObject == object.id && selectedAtoms.contains(atom.id);
                atoms_.push_back(RenderVertex{QVector3D(atom.position.x, atom.position.y, atom.position.z),
                                              colorForAtom(atom, object.colorScheme), elementRadius(atom.element),
                                              selected ? 1.0F : 0.0F, scale});
            }
        }
        if (object.representation == Representation::BallAndStick || object.representation == Representation::Lines) {
            bondVertices_.reserve(bondVertices_.size() + molecule.bonds().size() * 2U);
            for (const auto& bond : molecule.bonds()) {
                const auto& a = molecule.atoms()[bond.first].position;
                const auto& b = molecule.atoms()[bond.second].position;
                const QVector3D color(0.52F, 0.57F, 0.64F);
                bondVertices_.push_back({QVector3D(a.x, a.y, a.z), color});
                bondVertices_.push_back({QVector3D(b.x, b.y, b.z), color});
            }
        } else if (object.representation == Representation::CAlphaTrace || object.representation == Representation::Backbone) {
            const auto trace = object.representation == Representation::CAlphaTrace ? molecule.cAlphaTrace() : molecule.backboneTrace();
            bondVertices_.reserve(bondVertices_.size() + trace.size() * 2U);
            for (const auto& segment : trace) {
                const auto& a = molecule.atoms()[segment.first].position;
                const auto& b = molecule.atoms()[segment.second].position;
                const auto& sourceAtom = molecule.atoms()[segment.first];
                const QVector3D color = colorForAtom(sourceAtom, object.colorScheme);
                bondVertices_.push_back({QVector3D(a.x, a.y, a.z), color});
                bondVertices_.push_back({QVector3D(b.x, b.y, b.z), color});
            }
        } else if (object.representation == Representation::Cartoon || object.representation == Representation::Ribbon) {
            const bool ribbon = object.representation == Representation::Ribbon;
            appendCartoonGeometry(molecule, ribbon, [&](std::size_t atom) {
                return colorForAtom(molecule.atoms()[atom], object.colorScheme);
            }, ribbonVertices_, ribbonIndices_);
        }
        batch.atoms.count = static_cast<GLsizei>(atoms_.size() - batch.atoms.first);
        batch.bonds.count = static_cast<GLsizei>(bondVertices_.size() - batch.bonds.first);
        batch.ribbons.count = static_cast<GLsizei>(ribbonIndices_.size() - batch.ribbons.first);
        batches_.push_back(batch);
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
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void*>(offsetof(RenderVertex, scale)));
    atomBuffer_.release();
    atomVao_.release();

    bondVao_.bind();
    bondBuffer_.bind();
    bondBuffer_.allocate(bondVertices_.data(), static_cast<int>(bondVertices_.size() * sizeof(BondVertex)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BondVertex), reinterpret_cast<void*>(offsetof(BondVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(BondVertex), reinterpret_cast<void*>(offsetof(BondVertex, color)));
    bondBuffer_.release();
    bondVao_.release();

    ribbonVao_.bind();
    ribbonBuffer_.bind();
    ribbonBuffer_.allocate(ribbonVertices_.data(), static_cast<int>(ribbonVertices_.size() * sizeof(RibbonVertex)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RibbonVertex), reinterpret_cast<void*>(offsetof(RibbonVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(RibbonVertex), reinterpret_cast<void*>(offsetof(RibbonVertex, color)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(RibbonVertex), reinterpret_cast<void*>(offsetof(RibbonVertex, normal)));
    ribbonBuffer_.release();
    ribbonIndexBuffer_.bind();
    ribbonIndexBuffer_.allocate(ribbonIndices_.data(), static_cast<int>(ribbonIndices_.size() * sizeof(std::uint32_t)));
    // The element buffer binding is part of the VAO state.
    ribbonVao_.release();
    uploadedGeneration_ = scene_->generation();
}

void MolecularViewport::releaseGlResources() {
    transparencyBuffer_.reset();
    compositeVao_.destroy();
    compositeProgram_.reset();
    atomVao_.destroy();
    bondVao_.destroy();
    ribbonVao_.destroy();
    atomBuffer_.destroy();
    bondBuffer_.destroy();
    ribbonBuffer_.destroy();
    ribbonIndexBuffer_.destroy();
    atomProgram_.reset();
    bondProgram_.reset();
    ribbonProgram_.reset();
}

QMatrix4x4 MolecularViewport::modelViewMatrix() const {
    QMatrix4x4 view;
    view.translate(pan_);
    view.translate(0.0F, 0.0F, -distance_);
    view.rotate(orientation_);
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
    QVector3D minimum;
    QVector3D maximum;
    bool hasAtoms = false;
    for (const auto& object : scene_->objects()) {
        if (!object.visible || object.opacity <= 0.0 || !object.molecule) continue;
        for (const auto& atom : object.molecule->atoms()) {
            const QVector3D point(atom.position.x, atom.position.y, atom.position.z);
            if (!hasAtoms) {
                minimum = maximum = point;
                hasAtoms = true;
            } else {
                minimum.setX(std::min(minimum.x(), point.x()));
                minimum.setY(std::min(minimum.y(), point.y()));
                minimum.setZ(std::min(minimum.z(), point.z()));
                maximum.setX(std::max(maximum.x(), point.x()));
                maximum.setY(std::max(maximum.y(), point.y()));
                maximum.setZ(std::max(maximum.z(), point.z()));
            }
        }
    }
    if (hasAtoms) {
        sceneCenter_ = (minimum + maximum) * 0.5F;
        sceneRadius_ = std::max(1.0F, (maximum - minimum).length() * 0.5F);
        distance_ = std::max(6.0F, sceneRadius_ * 3.0F);
        zoomTarget_ = distance_;
        zoomTimer_.stop();
        pan_ = {};
    }
    update();
}

void MolecularViewport::setOrthographic(bool orthographic) {
    orthographic_ = orthographic;
    update();
}

void MolecularViewport::setViewPreset(ViewPreset preset) {
    switch (preset) {
    case ViewPreset::Front: orientation_ = orientationFor(0.F, 0.F); break;
    case ViewPreset::Back: orientation_ = orientationFor(180.F, 0.F); break;
    case ViewPreset::Left: orientation_ = orientationFor(-90.F, 0.F); break;
    case ViewPreset::Right: orientation_ = orientationFor(90.F, 0.F); break;
    case ViewPreset::Top: orientation_ = orientationFor(0.F, -90.F); break;
    case ViewPreset::Bottom: orientation_ = orientationFor(0.F, 90.F); break;
    }
    emit cameraChanged();
    update();
}

void MolecularViewport::setLabelMode(LabelMode mode) {
    labelMode_ = mode;
    update();
}

void MolecularViewport::drawLabels(const QMatrix4x4& mvp) {
    if (labelMode_ == LabelMode::None) return;
    const auto selected = scene_->selectedObjectId();
    const auto object = selected ? scene_->object(*selected) : std::nullopt;
    if (!object || !object->visible || object->opacity <= 0.0) return;
    const auto atoms = scene_->selectedAtoms();
    if (atoms.empty()) return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font = painter.font();
    font.setPointSizeF(std::max(9.0, font.pointSizeF()));
    painter.setFont(font);
    std::unordered_set<std::string> renderedResidues;
    std::size_t rendered = 0;
    for (const auto& atom : atoms) {
        if (rendered >= 200) break;
        QString label;
        if (labelMode_ == LabelMode::SelectedAtoms) {
            label = QStringLiteral("%1 %2:%3").arg(QString::fromStdString(atom.name),
                                                    QString::fromStdString(atom.residueName)).arg(atom.residueId);
        } else {
            const auto key = atom.chainId + ":" + std::to_string(atom.residueId) + ":" + atom.residueName;
            if (!renderedResidues.insert(key).second) continue;
            label = QStringLiteral("%1 %2 %3").arg(QString::fromStdString(atom.chainId.empty() ? "-" : atom.chainId),
                                                    QString::fromStdString(atom.residueName)).arg(atom.residueId);
        }
        const QVector4D clip = mvp * QVector4D(atom.position.x, atom.position.y, atom.position.z, 1.0F);
        if (clip.w() <= 0.0F) continue;
        const QVector3D ndc = clip.toVector3DAffine();
        if (ndc.z() < -1.0F || ndc.z() > 1.0F) continue;
        const QPointF position((ndc.x() * 0.5F + 0.5F) * width() + 7.0F,
                               (1.0F - (ndc.y() * 0.5F + 0.5F)) * height() - 7.0F);
        const QRectF bounds = painter.fontMetrics().boundingRect(label).adjusted(-4.0, -2.0, 4.0, 2.0);
        const QRectF background(position + bounds.topLeft(), bounds.size());
        painter.fillRect(background, QColor(8, 12, 18, 205));
        painter.setPen(QColor(235, 240, 248));
        painter.drawText(position, label);
        ++rendered;
    }
}

void MolecularViewport::mousePressEvent(QMouseEvent* event) {
    lastMousePosition_ = event->position();
    pressPosition_ = lastMousePosition_;
    dragging_ = false;
}

void MolecularViewport::mouseMoveEvent(QMouseEvent* event) {
    if (!event->buttons()) return;
    const auto current = event->position();
    if (!dragging_ && (current - pressPosition_).manhattanLength() < 3.0) return;
    const auto delta = current - lastMousePosition_;
    lastMousePosition_ = current;
    dragging_ = true;
    if (event->buttons() & Qt::LeftButton) {
        const auto axis = QVector3D(delta.y(), delta.x(), 0.0F);
        if (!axis.isNull()) {
            orientation_ = (QQuaternion::fromAxisAndAngle(axis, axis.length() * 0.43F) * orientation_).normalized();
        }
    } else if (event->buttons() & (Qt::MiddleButton | Qt::RightButton)) {
        const float scale = distance_ / std::max(200.0F, static_cast<float>(height())) * 1.2F;
        pan_ += QVector3D(delta.x() * scale, -delta.y() * scale, 0.0F);
    }
    emit cameraChanged();
    update();
}

void MolecularViewport::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && !dragging_ && (event->position() - pressPosition_).manhattanLength() < 5.0) {
        pickAtom(event->position().toPoint());
    }
}

void MolecularViewport::wheelEvent(QWheelEvent* event) {
    const auto pixels = event->pixelDelta().y();
    const auto exponent = std::clamp(pixels != 0 ? -pixels / 360.0F : -event->angleDelta().y() / 1200.0F,
                                     -0.55F, 0.55F);
    if (exponent == 0.F) return;
    zoomTarget_ = std::clamp(zoomTarget_ * std::exp(exponent), 1.5F, 1500.0F);
    if (!zoomTimer_.isActive() && zoomTarget_ != distance_) {
        zoomClock_.start();
        zoomTimer_.start();
    }
    event->accept();
}

void MolecularViewport::pickAtom(const QPoint& point) {
    const auto mvp = projectionMatrix() * modelViewMatrix();
    float bestDistance = 14.0F * 14.0F;
    float bestDepth = std::numeric_limits<float>::max();
    std::optional<ObjectId> bestObject;
    std::optional<std::uint64_t> bestAtom;
    for (const auto& object : scene_->objects()) {
        if (!object.visible || object.opacity <= 0.0 || !object.molecule) continue;
        for (const auto& atom : object.molecule->atoms()) {
            if (object.representation == Representation::CAlphaTrace && atom.name != "CA") continue;
            if (object.representation == Representation::Backbone && atom.name != "N" && atom.name != "CA" && atom.name != "C") continue;
            if ((object.representation == Representation::Cartoon || object.representation == Representation::Ribbon) && atom.name != "CA") continue;
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
                bestObject = object.id;
                bestAtom = atom.id;
            }
        }
    }
    if (bestObject && bestAtom) emit atomPicked(*bestObject, *bestAtom, QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier));
}

QVector3D MolecularViewport::colorForAtom(const Atom& atom, ColorScheme scheme) {
    switch (scheme) {
    case ColorScheme::Element: {
        const auto c = elementColor(atom.element);
        return QVector3D(c[0], c[1], c[2]);
    }
    case ColorScheme::Chain: return chainColor(atom.chainId);
    case ColorScheme::Residue: return chainColor(atom.residueName + std::to_string(atom.residueId));
    case ColorScheme::SecondaryStructure: {
        const auto color = secondaryStructureColor(atom.secondaryStructure);
        return QVector3D(color[0], color[1], color[2]);
    }
    }
    return QVector3D(0.8F, 0.8F, 0.8F);
}

} // namespace jlmol
