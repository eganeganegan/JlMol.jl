#include "gui/MolecularViewport.h"
#include "renderer/CartoonGeometry.h"

#include <QApplication>
#include <QColorSpace>
#include <QFontInfo>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QScopeGuard>
#include <QWheelEvent>
#include <QXmlStreamWriter>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <tuple>
#include <unordered_set>

namespace jlmol {
namespace {

constexpr int maxExportEdge = 8192;
constexpr qint64 maxExportPixels = 16 * 1024 * 1024;
constexpr auto invalidGeneration = ~std::uint64_t{0};
constexpr QPointF labelOffset(7.0, -7.0);
const QColor labelBackdrop(8, 12, 18, 205);
const QColor labelInk(235, 240, 248);

bool drawsAtoms(Representation representation) {
    return representation == Representation::BallAndStick || representation == Representation::SpaceFilling ||
           representation == Representation::Points;
}

bool drawsBonds(Representation representation) {
    return representation == Representation::BallAndStick || representation == Representation::Lines;
}

bool drawsObject(const MolecularObject& object) {
    return object.visible && object.opacity > 0.0 && object.molecule;
}

QVector3D vector(Vec3 position) { return {position.x, position.y, position.z}; }

// Buffer writes use half-open vertex ranges. Merge neighbors to keep group selection cheap.
using BufferRanges = std::vector<std::pair<std::size_t, std::size_t>>;

void mergeWrites(BufferRanges& writes) {
    std::sort(writes.begin(), writes.end());
    std::size_t count = 0;
    for (const auto range : writes) {
        if (count && range.first <= writes[count - 1].second) {
            writes[count - 1].second = std::max(writes[count - 1].second, range.second);
        } else {
            writes[count++] = range;
        }
    }
    writes.resize(count);
    if (count > 32) {
        writes.front().second = writes.back().second;
        writes.resize(1);
    }
}

template<class Vertex>
void writeVertices(QOpenGLBuffer& buffer, const std::vector<Vertex>& vertices, const BufferRanges& writes) {
    if (writes.empty()) return;
    buffer.bind();
    if (writes.size() == 1 && writes.front().first == 0 && writes.front().second == vertices.size()) {
        // A full replacement lets the driver hand out fresh storage while the previous draw finishes.
        buffer.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(Vertex)));
    } else {
        for (const auto& [first, end] : writes) {
            buffer.write(static_cast<int>(first * sizeof(Vertex)), vertices.data() + first,
                         static_cast<int>((end - first) * sizeof(Vertex)));
        }
    }
    buffer.release();
}

constexpr auto geometryFragmentPrelude = R"GLSL(#version 410 core
uniform float opacity;
uniform int renderPass;
uniform int lightingPreset;
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
    vec4 opaque = texelFetch(opaqueTexture, pixel, 0);
    vec4 accumulation = texelFetch(accumulationTexture, pixel, 0);
    float revealage = texelFetch(revealageTexture, pixel, 0).r;
    vec3 transparent = accumulation.rgb / max(accumulation.a, 1.0e-12);
    fragColor = vec4(mix(transparent, opaque.rgb, revealage), 1.0 - revealage * (1.0 - opaque.a));
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
out vec3 atomColor;
out float atomSelected;
out float relativeSize;
void main() {
    vec4 eye = view * vec4(position, 1.0);
    gl_Position = mvp * vec4(position, 1.0);
    relativeSize = radius * atomScale / max(1.0, -eye.z);
    atomColor = color;
    atomSelected = selected;
}
)GLSL";

constexpr auto atomGeometryShader = R"GLSL(
#version 410 core
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
uniform vec2 viewportSize;
uniform float styleScale;
in vec3 atomColor[];
in float atomSelected[];
in float relativeSize[];
out vec3 vertexColor;
out float isSelected;
out vec2 spritePosition;
void main() {
    vec4 center = gl_in[0].gl_Position;
    if (center.w <= 0.0 || abs(center.z) > center.w) return;
    float diameter = clamp(relativeSize[0] * viewportSize.y, 2.0 * styleScale, 180.0 * styleScale);
    for (int i = 0; i < 4; ++i) {
        vec2 corner = vec2(i & 1, i >> 1) * 2.0 - 1.0;
        gl_Position = center + vec4(corner * diameter * center.w / viewportSize, 0.0, 0.0);
        vertexColor = atomColor[0];
        isSelected = atomSelected[0];
        spritePosition = corner;
        EmitVertex();
    }
    EndPrimitive();
}
)GLSL";

constexpr auto atomFragmentShader = R"GLSL(
in vec3 vertexColor;
in float isSelected;
in vec2 spritePosition;
out vec4 fragColor;
void main() {
    vec2 p = spritePosition;
    float r2 = dot(p, p);
    if (r2 > 1.0) discard;
    vec3 normal = vec3(p.x, p.y, sqrt(max(0.0, 1.0 - r2)));
    float diffuse = 0.30 + 0.70 * max(0.0, dot(normalize(vec3(-0.35, 0.5, 1.0)), normal));
    if (lightingPreset == 1) diffuse = 0.55 + 0.45 * diffuse;
    if (lightingPreset == 2) diffuse = 1.0;
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
    lit += 0.14 * highlight * mix(vertexColor, vec3(1.0), 0.45);
    if (lightingPreset == 1) lit = mix(lit, vertexColor, 0.50);
    if (lightingPreset == 2) lit = vertexColor;
    fragColor = encodeFragment(clamp(lit, 0.0, 1.0));
}
)GLSL";

std::unique_ptr<QOpenGLShaderProgram> makeProgram(const char* vertex, const QByteArray& fragment, const char* geometry = nullptr) {
    auto program = std::make_unique<QOpenGLShaderProgram>();
    if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex) ||
        (geometry && !program->addShaderFromSourceCode(QOpenGLShader::Geometry, geometry)) ||
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

std::vector<QVector3D> sequenceColors(const std::vector<Atom>& atoms) {
    std::vector<std::size_t> residues;
    residues.reserve(atoms.size());
    std::size_t count = 0;
    const auto residue = [](const Atom& atom) {
        return std::tie(atom.modelId, atom.chainId, atom.residueId, atom.residueName);
    };
    for (std::size_t i = 0; i < atoms.size(); ++i) {
        if (i == 0 || residue(atoms[i]) != residue(atoms[i - 1])) ++count;
        residues.push_back(count - 1);
    }
    std::vector<QVector3D> colors;
    colors.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto hue = count == 1 ? 0.55 : 0.68 * (1.0 - static_cast<double>(i) / (count - 1));
        const auto color = QColor::fromHsvF(hue, 0.82, 0.94);
        colors.emplace_back(color.redF(), color.greenF(), color.blueF());
    }
    std::vector<QVector3D> atomColors;
    atomColors.reserve(atoms.size());
    for (const auto index : residues) atomColors.push_back(colors[index]);
    return atomColors;
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
    glEnable(GL_MULTISAMPLE);
    GLfloat lineWidths[2]{1.0F, 1.0F};
    glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, lineWidths);
    lineWidth_ = std::clamp(3.0F, lineWidths[0], lineWidths[1]);
    maxLineWidth_ = lineWidths[1];
    atomProgram_ = makeProgram(atomVertexShader, QByteArray(geometryFragmentPrelude) + atomFragmentShader, atomGeometryShader);
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
    uploadedGeneration_ = invalidGeneration;
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
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    const QSize pixels(viewport[2], viewport[3]);
    if (pixels.isEmpty()) return;
    const auto view = modelViewMatrix();
    const auto mvp = projectionMatrix(size()) * view;
    (void)renderGeometry(view, mvp, pixels, 1.0F, defaultFramebufferObject(), background_, transparencyBuffer_, false);
    if (labelMode_ != LabelMode::None) {
        QPainter painter(this);
        drawLabels(painter, mvp, size(), 1.0F);
    }
}

bool MolecularViewport::renderGeometry(const QMatrix4x4& view, const QMatrix4x4& mvp, QSize pixels, float styleScale,
                                       GLuint target, const QColor& background,
                                       std::unique_ptr<QOpenGLFramebufferObject>& transparency, bool exporting) {
    const bool transparent = std::any_of(batches_.begin(), batches_.end(), [](const auto& batch) {
        return batch.opacity < 1.0F && (batch.atoms.count || batch.bonds.count || batch.ribbons.count);
    });
    glViewport(0, 0, pixels.width(), pixels.height());
    glClearColor(background.redF(), background.greenF(), background.blueF(), background.alphaF());
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    if (transparent) {
        if (!transparency || transparency->size() != pixels) transparency = makeTransparencyBuffer(pixels);
        if (drawTransparentScene(view, mvp, pixels, styleScale, target, *transparency)) return true;
        if (exporting) return false;
    } else {
        transparency.reset();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawGeometry(view, mvp, pixels, styleScale, 0, false);
    return true;
}

void MolecularViewport::drawGeometry(const QMatrix4x4& view, const QMatrix4x4& mvp, QSize pixels, float styleScale,
                                     int pass, bool opaqueOnly) {
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
        ribbonProgram_->setUniformValue("lightingPreset", static_cast<int>(lighting_));
        ribbonVao_.bind();
        draw(*ribbonProgram_, GL_TRIANGLES, &DrawBatch::ribbons);
        ribbonVao_.release();
        ribbonProgram_->release();
    }
    if (!bondVertices_.empty()) {
        bondProgram_->bind();
        bondProgram_->setUniformValue("mvp", mvp);
        bondVao_.bind();
        glLineWidth(std::clamp(lineWidth_ * styleScale, 1.0F, maxLineWidth_));
        draw(*bondProgram_, GL_LINES, &DrawBatch::bonds);
        bondVao_.release();
        bondProgram_->release();
    }

    if (!atoms_.empty()) {
        atomProgram_->bind();
        atomProgram_->setUniformValue("mvp", mvp);
        atomProgram_->setUniformValue("view", view);
        atomProgram_->setUniformValue("viewportSize", QVector2D(pixels.width(), pixels.height()));
        atomProgram_->setUniformValue("styleScale", styleScale);
        atomProgram_->setUniformValue("lightingPreset", static_cast<int>(lighting_));
        atomVao_.bind();
        draw(*atomProgram_, GL_POINTS, &DrawBatch::atoms);
        atomVao_.release();
        atomProgram_->release();
    }
}

std::unique_ptr<QOpenGLFramebufferObject> MolecularViewport::makeTransparencyBuffer(QSize pixels) {
    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::Depth);
    format.setInternalTextureFormat(GL_RGBA8);
    auto buffer = std::make_unique<QOpenGLFramebufferObject>(pixels, format);
    if (buffer->isValid()) {
        buffer->addColorAttachment(pixels, GL_RGBA16F);
        buffer->addColorAttachment(pixels, GL_R16F);
    }
    if (!buffer->isValid() || buffer->textures().size() != 3) qWarning("JlMol: transparency framebuffer unavailable.");
    return buffer;
}

bool MolecularViewport::drawTransparentScene(const QMatrix4x4& view, const QMatrix4x4& mvp, QSize pixels,
                                             float styleScale, GLuint target, QOpenGLFramebufferObject& buffer) {
    const auto textures = buffer.textures();
    if (!buffer.isValid() || textures.size() != 3 || !buffer.bind()) return false;

    // The three color targets share the same opaque depth buffer.
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawGeometry(view, mvp, pixels, styleScale, 0);

    constexpr GLfloat clear[4]{0, 0, 0, 0};
    constexpr GLfloat reveal[4]{1, 1, 1, 1};
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glDrawBuffer(GL_COLOR_ATTACHMENT1);
    glClearBufferfv(GL_COLOR, 0, clear);
    glBlendFunc(GL_ONE, GL_ONE);
    drawGeometry(view, mvp, pixels, styleScale, 1);
    glDrawBuffer(GL_COLOR_ATTACHMENT2);
    glClearBufferfv(GL_COLOR, 0, reveal);
    glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
    drawGeometry(view, mvp, pixels, styleScale, 2);

    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
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

bool MolecularViewport::supportsImageSize(QSize pixels) {
    return !pixels.isEmpty() && pixels.width() <= maxExportEdge && pixels.height() <= maxExportEdge &&
           qint64(pixels.width()) * pixels.height() <= maxExportPixels;
}

QImage MolecularViewport::renderImage(QSize pixels, bool transparentBackground, bool labels, QString& error,
                                      const MolecularObject* frame) {
    error.clear();
    if (!supportsImageSize(pixels)) {
        error = QStringLiteral("Use dimensions up to 8,192 pixels per side and 16,777,216 pixels total.");
        return {};
    }
    if (!isValid() || !atomProgram_) {
        error = QStringLiteral("OpenGL rendering is unavailable. Show the molecular viewport before exporting.");
        return {};
    }
    makeCurrent();
    if (QOpenGLContext::currentContext() != context()) {
        error = QStringLiteral("Could not activate the molecular viewport for export.");
        return {};
    }
    GLint previousViewport[4];
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    const auto restore = qScopeGuard([&] {
        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        doneCurrent();
        update();
    });
    GLint textureLimit = 0, renderbufferLimit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &textureLimit);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &renderbufferLimit);
    const auto maxEdge = std::min(textureLimit, renderbufferLimit);
    if (pixels.width() > maxEdge || pixels.height() > maxEdge) {
        error = QStringLiteral("This GPU supports export dimensions up to %1 pixels per side.").arg(maxEdge);
        return {};
    }
    auto renderSize = pixels;
    if (supportsImageSize(pixels * 2) && pixels.width() * 2 <= maxEdge && pixels.height() * 2 <= maxEdge) {
        renderSize *= 2;
    }
    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::Depth);
    format.setInternalTextureFormat(GL_RGBA8);
    QOpenGLFramebufferObject target(renderSize, format);
    if (!target.isValid()) {
        error = QStringLiteral("Could not allocate the export image. Try smaller dimensions.");
        return {};
    }
    if (frame || uploadedGeneration_ != scene_->generation()) rebuildGeometry(frame);
    const auto view = modelViewMatrix();
    const auto mvp = projectionMatrix(pixels) * view;
    const auto styleScale = static_cast<float>(renderSize.height()) / std::max(1.0, height() * devicePixelRatioF());
    std::unique_ptr<QOpenGLFramebufferObject> transparency;
    if (!renderGeometry(view, mvp, renderSize, styleScale, target.handle(),
                        transparentBackground ? QColor(Qt::transparent) : background_, transparency, true)) {
        error = QStringLiteral("Could not allocate the transparency buffers. Try smaller dimensions.");
        return {};
    }
    QImage image(renderSize, QImage::Format_RGBA8888_Premultiplied);
    if (image.isNull()) {
        error = QStringLiteral("Could not allocate memory for the export image.");
        return {};
    }
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, renderSize.width(), renderSize.height(), GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
    if (glGetError() != GL_NO_ERROR) {
        error = QStringLiteral("OpenGL could not read the rendered image.");
        return {};
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    image.flip(Qt::Vertical);
#else
    image = image.mirrored(false, true);
#endif
    if (renderSize != pixels) image = image.scaled(pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (image.isNull()) {
        error = QStringLiteral("Could not allocate memory for the finished image.");
        return {};
    }
    image.setColorSpace(QColorSpace::SRgb);
    if (labels && labelMode_ != LabelMode::None) {
        QPainter painter(&image);
        drawLabels(painter, mvp, pixels, static_cast<float>(pixels.height()) / std::max(1, height()), frame);
    }
    return image;
}

void MolecularViewport::sceneChanged() {
    update();
}

MolecularViewport::RedrawTiming MolecularViewport::measureRedraw() {
    RedrawTiming result;
    redrawTiming_ = &result;
    const auto reset = qScopeGuard([this] { redrawTiming_ = nullptr; });
    QElapsedTimer elapsed;
    elapsed.start();
    (void)grabFramebuffer();
    result.totalMs = elapsed.nsecsElapsed() / 1.0e6;
    return result;
}

void MolecularViewport::rebuildGeometry(const MolecularObject* frame) {
    if (!context()) return;
    QElapsedTimer elapsed;
    if (redrawTiming_) elapsed.start();
    const auto generation = scene_->generation();
    auto objects = scene_->objects();
    const auto selectedObject = scene_->selectedObjectId();
    auto selectedAtomIds = scene_->selectedAtomIds();
    const auto rememberGeometry = [&] {
        renderedObjects_ = std::move(objects);
        renderedSelectionObject_ = selectedObject;
        renderedSelection_ = std::move(selectedAtomIds);
        uploadedGeneration_ = generation;
    };
    if (!frame && updateExistingGeometry(objects, selectedObject, selectedAtomIds, elapsed)) {
        rememberGeometry();
        return;
    }
    atoms_.clear();
    bondVertices_.clear();
    ribbonVertices_.clear();
    ribbonIndices_.clear();
    batches_.clear();
    const std::unordered_set<std::uint64_t> selectedAtoms(selectedAtomIds.begin(), selectedAtomIds.end());
    for (const auto& original : objects) {
        const auto& object = frame && frame->id == original.id ? *frame : original;
        if (!drawsObject(object)) continue;
        DrawBatch batch;
        batch.opacity = static_cast<float>(object.opacity);
        batch.atoms.first = static_cast<GLint>(atoms_.size());
        batch.bonds.first = static_cast<GLint>(bondVertices_.size());
        batch.ribbons.first = static_cast<GLint>(ribbonIndices_.size());
        const auto& molecule = *object.molecule;
        const auto& atomList = molecule.atoms();
        const auto sequence = object.colorScheme == ColorScheme::Sequence ? sequenceColors(atomList) : std::vector<QVector3D>{};
        const bool coloredBonds = object.atomColors || !sequence.empty();
        const auto atomColor = [&](std::size_t index) {
            if (object.atomColors) {
                const auto& rgb = (*object.atomColors)[index];
                return QVector3D(rgb[0], rgb[1], rgb[2]);
            }
            if (!sequence.empty()) return sequence[index];
            return colorForAtom(atomList[index], object.colorScheme);
        };
        const bool showsAtoms = drawsAtoms(object.representation);
        if (showsAtoms) {
            const float scale = object.representation == Representation::SpaceFilling ? 2.15F :
                                object.representation == Representation::Points ? 0.28F : 0.72F;
            atoms_.reserve(atoms_.size() + molecule.atoms().size());
            for (std::size_t index = 0; index < atomList.size(); ++index) {
                const auto& atom = atomList[index];
                const bool selected = selectedObject == object.id && selectedAtoms.contains(atom.id);
                atoms_.push_back(RenderVertex{QVector3D(atom.position.x, atom.position.y, atom.position.z),
                                              atomColor(index), elementRadius(atom.element),
                                              selected ? 1.0F : 0.0F, scale});
            }
        }
        if (drawsBonds(object.representation)) {
            bondVertices_.reserve(bondVertices_.size() + molecule.bonds().size() * 2U);
            for (const auto& bond : molecule.bonds()) {
                const auto& a = molecule.atoms()[bond.first].position;
                const auto& b = molecule.atoms()[bond.second].position;
                const QVector3D neutral(0.52F, 0.57F, 0.64F);
                bondVertices_.push_back({QVector3D(a.x, a.y, a.z), coloredBonds ? atomColor(bond.first) : neutral});
                bondVertices_.push_back({QVector3D(b.x, b.y, b.z), coloredBonds ? atomColor(bond.second) : neutral});
            }
        } else if (object.representation == Representation::CAlphaTrace || object.representation == Representation::Backbone) {
            const auto trace = object.representation == Representation::CAlphaTrace ? molecule.cAlphaTrace() : molecule.backboneTrace();
            bondVertices_.reserve(bondVertices_.size() + trace.size() * 2U);
            for (const auto& segment : trace) {
                const auto& a = molecule.atoms()[segment.first].position;
                const auto& b = molecule.atoms()[segment.second].position;
                const QVector3D color = atomColor(segment.first);
                bondVertices_.push_back({QVector3D(a.x, a.y, a.z), color});
                bondVertices_.push_back({QVector3D(b.x, b.y, b.z), coloredBonds ? atomColor(segment.second) : color});
            }
        } else if (object.representation == Representation::Cartoon || object.representation == Representation::Ribbon) {
            const bool ribbon = object.representation == Representation::Ribbon;
            appendCartoonGeometry(molecule, ribbon, atomColor, ribbonVertices_, ribbonIndices_);
        }
        if (!showsAtoms && selectedObject == object.id && !selectedAtoms.empty() && selectedAtoms.size() <= 4) {
            // Keep an analysis point or measured atom pair visible in ribbon and trace modes.
            auto ordered = selectedAtomIds;
            std::sort(ordered.begin(), ordered.end());
            ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());
            for (const auto id : ordered) {
                if (const auto* atom = molecule.atom(id)) {
                    atoms_.push_back({vector(atom->position), atomColor(atom - atomList.data()),
                                      elementRadius(atom->element), 1.0F, 0.4F});
                }
            }
        }
        batch.atoms.count = static_cast<GLsizei>(atoms_.size() - batch.atoms.first);
        batch.bonds.count = static_cast<GLsizei>(bondVertices_.size() - batch.bonds.first);
        batch.ribbons.count = static_cast<GLsizei>(ribbonIndices_.size() - batch.ribbons.first);
        batches_.push_back(batch);
    }

    startUploadTiming(elapsed, atoms_.size() * sizeof(RenderVertex) + bondVertices_.size() * sizeof(BondVertex) +
                               ribbonVertices_.size() * sizeof(RibbonVertex) + ribbonIndices_.size() * sizeof(std::uint32_t));
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
    finishUploadTiming(elapsed);
    if (frame) uploadedGeneration_ = invalidGeneration;
    else rememberGeometry();
}

bool MolecularViewport::updateExistingGeometry(const std::vector<MolecularObject>& objects,
                                               std::optional<ObjectId> selectedObject,
                                               const std::vector<std::uint64_t>& selectedAtoms, QElapsedTimer& elapsed) {
    if (uploadedGeneration_ == invalidGeneration || objects.size() != renderedObjects_.size()) return false;
    const bool selectionChanged = selectedObject != renderedSelectionObject_ || selectedAtoms != renderedSelection_;
    std::size_t batchIndex = 0;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        const auto& object = objects[i];
        const auto& previous = renderedObjects_[i];
        const bool visible = drawsObject(object);
        if (object.id != previous.id || visible != drawsObject(previous)) return false;
        if (!visible) continue;
        const auto& batch = batches_[batchIndex++];
        if (object.representation != previous.representation || object.colorScheme != previous.colorScheme ||
            object.atomColors != previous.atomColors) return false;
        if (!drawsAtoms(object.representation)) {
            const bool addMarkers = selectedObject == object.id && !selectedAtoms.empty() && selectedAtoms.size() <= 4;
            const bool oldMarkers = batch.atoms.count != 0;
            if (selectionChanged && (addMarkers || oldMarkers)) return false;
            // Ribbon and trace connectivity can depend on the displayed coordinates.
            if (object.molecule != previous.molecule && (object.representation != Representation::Lines || oldMarkers)) return false;
        }
    }

    BufferRanges atomWrites, bondWrites;
    batchIndex = 0;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        const auto& object = objects[i];
        if (!drawsObject(object)) continue;
        auto& batch = batches_[batchIndex++];
        batch.opacity = static_cast<float>(object.opacity);
        const auto& molecule = *object.molecule;
        if (object.molecule != renderedObjects_[i].molecule) {
            // The scene preserves atom and bond order across frame snapshots of the same object.
            if (batch.atoms.count) {
                auto vertex = atoms_.begin() + batch.atoms.first;
                for (const auto& atom : molecule.atoms()) (vertex++)->position = vector(atom.position);
                atomWrites.emplace_back(batch.atoms.first, batch.atoms.first + batch.atoms.count);
            }
            if (batch.bonds.count) {
                auto vertex = bondVertices_.begin() + batch.bonds.first;
                for (const auto& bond : molecule.bonds()) {
                    (vertex++)->position = vector(molecule.atoms()[bond.first].position);
                    (vertex++)->position = vector(molecule.atoms()[bond.second].position);
                }
                bondWrites.emplace_back(batch.bonds.first, batch.bonds.first + batch.bonds.count);
            }
        }
        if (selectionChanged && drawsAtoms(object.representation)) {
            const auto mark = [&](const std::vector<std::uint64_t>& ids, float value) {
                for (const auto id : ids) {
                    const auto* atom = molecule.atom(id);
                    if (!atom) continue;
                    const auto index = static_cast<std::size_t>(batch.atoms.first) + (atom - molecule.atoms().data());
                    if (atoms_[index].selected == value) continue;
                    atoms_[index].selected = value;
                    atomWrites.emplace_back(index, index + 1);
                }
            };
            if (renderedSelectionObject_ == object.id) mark(renderedSelection_, 0.0F);
            if (selectedObject == object.id) mark(selectedAtoms, 1.0F);
        }
    }
    mergeWrites(atomWrites);
    mergeWrites(bondWrites);
    std::size_t bytes = 0;
    if (redrawTiming_) {
        for (const auto& [first, end] : atomWrites) bytes += (end - first) * sizeof(RenderVertex);
        for (const auto& [first, end] : bondWrites) bytes += (end - first) * sizeof(BondVertex);
    }
    startUploadTiming(elapsed, bytes);
    writeVertices(atomBuffer_, atoms_, atomWrites);
    writeVertices(bondBuffer_, bondVertices_, bondWrites);
    finishUploadTiming(elapsed);
    return true;
}

void MolecularViewport::startUploadTiming(QElapsedTimer& elapsed, std::size_t bytes) {
    if (!redrawTiming_) return;
    redrawTiming_->geometryMs = elapsed.nsecsElapsed() / 1.0e6;
    redrawTiming_->uploadBytes = bytes;
    if (bytes) {
        glFinish();
        elapsed.start();
    }
}

void MolecularViewport::finishUploadTiming(const QElapsedTimer& elapsed) {
    if (redrawTiming_ && redrawTiming_->uploadBytes) {
        glFinish();
        redrawTiming_->uploadMs = elapsed.nsecsElapsed() / 1.0e6;
    }
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

QMatrix4x4 MolecularViewport::projectionMatrix(QSize pixels) const {
    QMatrix4x4 projection;
    const float aspect = static_cast<float>(std::max(1, pixels.width())) / static_cast<float>(std::max(1, pixels.height()));
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

void MolecularViewport::setBackground(const QColor& color) {
    background_ = color;
    update();
}

void MolecularViewport::setLighting(LightingPreset preset) {
    lighting_ = preset;
    update();
}

MolecularViewport::CameraState MolecularViewport::cameraState() const {
    return {orientation_, pan_, sceneCenter_, distance_, sceneRadius_, orthographic_, labelMode_, background_, lighting_};
}

void MolecularViewport::restoreCamera(const CameraState& state) {
    zoomTimer_.stop();
    orientation_ = state.orientation;
    pan_ = state.pan;
    sceneCenter_ = state.center;
    distance_ = zoomTarget_ = state.distance;
    sceneRadius_ = state.radius;
    orthographic_ = state.orthographic;
    labelMode_ = state.labels;
    background_ = state.background;
    lighting_ = state.lighting;
    emit cameraChanged();
    update();
}

std::vector<MolecularViewport::ProjectedLabel> MolecularViewport::projectedLabels(
        const QMatrix4x4& mvp, QSize pixels, const MolecularObject* frame) const {
    const auto selected = scene_->selectedObjectId();
    const auto object = selected ? scene_->object(*selected) : std::nullopt;
    if (!object || !object->visible || object->opacity <= 0.0) return {};
    const auto atoms = scene_->selectedAtoms();
    std::vector<ProjectedLabel> projected;
    projected.reserve(std::min<std::size_t>(atoms.size(), 200));
    std::unordered_set<std::string> renderedResidues;
    for (const auto& atom : atoms) {
        if (projected.size() >= 200) break;
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
        auto position = atom.position;
        if (frame && frame->id == object->id) {
            const auto* current = frame->molecule->atom(atom.id);
            if (!current) continue;
            position = current->position;
        }
        const QVector4D clip = mvp * QVector4D(position.x, position.y, position.z, 1.0F);
        if (clip.w() <= 0.0F) continue;
        const QVector3D ndc = clip.toVector3DAffine();
        if (ndc.z() < -1.0F || ndc.z() > 1.0F) continue;
        projected.push_back({{(ndc.x() * 0.5F + 0.5F) * pixels.width(),
                              (1.0F - (ndc.y() * 0.5F + 0.5F)) * pixels.height()}, std::move(label)});
    }
    return projected;
}

void MolecularViewport::drawLabels(QPainter& painter, const QMatrix4x4& mvp, QSize pixels, float scale,
                                   const MolecularObject* frame) {
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(labelFont());
    painter.setPen(labelInk);
    for (const auto& label : projectedLabels(mvp, pixels, frame)) {
        const QRectF bounds = painter.fontMetrics().boundingRect(label.text).adjusted(-4.0, -2.0, 4.0, 2.0);
        painter.save();
        painter.translate(label.position);
        painter.scale(scale, scale);
        painter.fillRect(bounds.translated(labelOffset), labelBackdrop);
        painter.drawText(labelOffset, label.text);
        painter.restore();
    }
}

QFont MolecularViewport::labelFont() const {
    auto label = font();
    label.setPointSizeF(std::max(9.0, label.pointSizeF()));
    label.setPixelSize(std::max(1, QFontInfo(label).pixelSize()));
    return label;
}

void MolecularViewport::writeSvgLabels(QXmlStreamWriter& xml, QSize pixels) const {
    if (labelMode_ == LabelMode::None) return;
    const auto mvp = projectionMatrix(pixels) * modelViewMatrix();
    const auto font = labelFont();
    const QFontMetrics metrics(font);
    const auto scale = double(pixels.height()) / std::max(1, height());
    const auto number = [](double value) { return QString::number(value, 'g', 10); };
    xml.writeStartElement(QStringLiteral("g"));
    xml.writeAttribute(QStringLiteral("id"), QStringLiteral("labels"));
    xml.writeAttribute(QStringLiteral("font-family"), QFontInfo(font).family());
    xml.writeAttribute(QStringLiteral("font-size"), QString::number(font.pixelSize()));
    for (const auto& label : projectedLabels(mvp, pixels, nullptr)) {
        const auto rect = QRectF(metrics.boundingRect(label.text)).adjusted(-4.0, -2.0, 4.0, 2.0).translated(labelOffset);
        xml.writeStartElement(QStringLiteral("g"));
        xml.writeAttribute(QStringLiteral("transform"), QStringLiteral("translate(%1 %2) scale(%3)")
            .arg(number(label.position.x()), number(label.position.y()), number(scale)));
        xml.writeEmptyElement(QStringLiteral("rect"));
        xml.writeAttribute(QStringLiteral("x"), number(rect.x()));
        xml.writeAttribute(QStringLiteral("y"), number(rect.y()));
        xml.writeAttribute(QStringLiteral("width"), number(rect.width()));
        xml.writeAttribute(QStringLiteral("height"), number(rect.height()));
        xml.writeAttribute(QStringLiteral("fill"), labelBackdrop.name());
        xml.writeAttribute(QStringLiteral("fill-opacity"), number(labelBackdrop.alphaF()));
        xml.writeStartElement(QStringLiteral("text"));
        xml.writeAttribute(QStringLiteral("x"), number(labelOffset.x()));
        xml.writeAttribute(QStringLiteral("y"), number(labelOffset.y()));
        xml.writeAttribute(QStringLiteral("fill"), labelInk.name());
        xml.writeCharacters(label.text);
        xml.writeEndElement();
        xml.writeEndElement();
    }
    xml.writeEndElement();
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
    const auto mvp = projectionMatrix(size()) * modelViewMatrix();
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
    case ColorScheme::Sequence:
    case ColorScheme::Custom: break;
    }
    return QVector3D(0.8F, 0.8F, 0.8F);
}

} // namespace jlmol
