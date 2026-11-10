#include "renderer/CartoonGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace jlmol {
namespace {

constexpr std::uint32_t sides = 10;

struct Section {
    float width;
    float thickness;
};

struct Ring {
    QVector3D center;
    QVector3D tangent;
    QVector3D side;
    QVector3D normal;
    QVector3D color;
    Section section{};
};

QVector3D vector(const Vec3& v) { return {v.x, v.y, v.z}; }

QVector3D projected(const QVector3D& direction, const QVector3D& tangent) {
    return direction - QVector3D::dotProduct(direction, tangent) * tangent;
}

Section blend(Section first, Section second, float t) {
    return {std::lerp(first.width, second.width, t), std::lerp(first.thickness, second.thickness, t)};
}

Section profile(SecondaryStructure structure, bool ribbon) {
    if (ribbon) return {0.52F, 0.095F};
    switch (structure) {
    case SecondaryStructure::Helix: return {0.83F, 0.12F};
    case SecondaryStructure::Sheet: return {0.73F, 0.11F};
    case SecondaryStructure::Coil: return {0.22F, 0.22F};
    }
    return {0.22F, 0.22F};
}

void applyProfiles(std::vector<Ring>& rings, std::span<const RibbonFrame> frames, bool ribbon, unsigned subdivisions) {
    rings.front().section = profile(frames.front().secondaryStructure, ribbon);
    for (std::size_t start = 0; start < frames.size();) {
        const auto structure = frames[start].secondaryStructure;
        std::size_t end = start + 1;
        while (end < frames.size() && frames[end].secondaryStructure == structure) ++end;
        const auto base = profile(structure, ribbon);
        const auto entry = rings[start].section;
        const auto transition = std::min<std::size_t>(end - start, subdivisions / 3);
        const bool arrow = !ribbon && structure == SecondaryStructure::Sheet;
        const auto arrowLength = std::min<std::size_t>(end - start, subdivisions);
        const auto shoulder = end - arrowLength + std::max<std::size_t>(1, arrowLength / 4);
        for (std::size_t i = start + 1; i <= end; ++i) {
            const float t = std::min(1.F, static_cast<float>(i - start) / transition);
            auto section = blend(entry, base, t * t * (3.F - 2.F * t));
            if (arrow && shoulder < end && i >= end - arrowLength) {
                const Section head{1.16F, 0.12F};
                const Section tip = end == frames.size() ? Section{0.045F, 0.045F} : Section{0.19F, 0.16F};
                if (i <= shoulder) {
                    const float widen = static_cast<float>(i - (end - arrowLength)) / (shoulder - (end - arrowLength));
                    section = blend(section, head, widen);
                } else {
                    const float taper = static_cast<float>(i - shoulder) / (end - shoulder);
                    section = blend(head, tip, taper);
                }
            }
            rings[i].section = section;
        }
        start = end;
    }
}

void appendStrip(std::span<const RibbonFrame> frames, bool ribbon, unsigned subdivisions,
                 const std::function<QVector3D(std::size_t)>& colorForAtom,
                 std::vector<RibbonVertex>& vertices, std::vector<std::uint32_t>& indices) {
    std::vector<Ring> rings(frames.size() + 1);
    for (std::size_t i = 0; i < rings.size(); ++i) {
        rings[i].center = i < frames.size() ? vector(frames[i].first) : vector(frames.back().second);
    }
    applyProfiles(rings, frames, ribbon, subdivisions);
    for (std::size_t i = 0; i < rings.size(); ++i) {
        const auto before = rings[i ? i - 1 : i].center;
        const auto after = rings[std::min(i + 1, rings.size() - 1)].center;
        auto& ring = rings[i];
        const auto& frame = frames[std::min(i, frames.size() - 1)];
        ring.tangent = (after - before).normalized();
        if (ring.tangent.isNull()) ring.tangent = (vector(frame.second) - vector(frame.first)).normalized();
        if (ring.tangent.isNull()) ring.tangent = {0.F, 0.F, 1.F};
        auto side = projected(i ? rings[i - 1].side : vector(frame.side), ring.tangent).normalized();
        if (side.isNull()) {
            const QVector3D reference = std::abs(ring.tangent.z()) < 0.85F ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0);
            side = QVector3D::crossProduct(ring.tangent, reference).normalized();
        }
        if (!ribbon && frame.secondaryStructure == SecondaryStructure::Helix && i > 0 && i < frames.size()) {
            // Keep the broad face aimed out from the helix, rather than allowing
            // a transported cross-section to gradually twist the band edge-on.
            const auto bend = (after - ring.center).normalized() - (ring.center - before).normalized();
            auto guide = QVector3D::crossProduct(ring.tangent, bend).normalized();
            if (!guide.isNull()) {
                if (QVector3D::dotProduct(guide, side) < 0.F) guide = -guide;
                side = (0.25F * side + 0.75F * guide).normalized();
            }
        }
        ring.side = side;
        ring.normal = QVector3D::crossProduct(ring.tangent, side).normalized();
        ring.color = colorForAtom(frame.sourceAtom);
    }

    const auto first = static_cast<std::uint32_t>(vertices.size());
    std::array<std::pair<float, float>, sides> circle{};
    for (std::uint32_t j = 0; j < sides; ++j) {
        const float angle = 2.F * std::numbers::pi_v<float> * j / sides;
        circle[j] = {std::cos(angle), std::sin(angle)};
    }
    for (const auto& ring : rings) {
        for (const auto [x, y] : circle) {
            const auto pos = ring.center + ring.side * (x * ring.section.width) +
                             ring.normal * (y * ring.section.thickness);
            const auto normal = (ring.side * (x / ring.section.width) +
                                 ring.normal * (y / ring.section.thickness)).normalized();
            vertices.push_back({pos, ring.color, normal});
        }
    }
    for (std::uint32_t i = 0; i < frames.size(); ++i) {
        const auto row = first + i * sides;
        for (std::uint32_t j = 0; j < sides; ++j) {
            const auto next = (j + 1) % sides;
            const auto a = row + j;
            const auto b = row + next;
            indices.insert(indices.end(), {a, b, b + sides, a, b + sides, a + sides});
        }
    }
    for (const bool start : {true, false}) {
        const auto& ring = start ? rings.front() : rings.back();
        const auto normal = start ? -ring.tangent : ring.tangent;
        const auto center = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back({ring.center, ring.color, normal});
        const auto row = start ? first : first + static_cast<std::uint32_t>(frames.size()) * sides;
        for (std::uint32_t j = 0; j < sides; ++j) {
            // Cap normals are separate from the shared side normals.
            const auto position = vertices[row + j].position;
            vertices.push_back({position, ring.color, normal});
            const auto a = center + 1 + j;
            const auto b = center + 1 + (j + 1) % sides;
            if (start) indices.insert(indices.end(), {center, b, a});
            else indices.insert(indices.end(), {center, a, b});
        }
    }
}

} // namespace

void appendCartoonGeometry(const Molecule& molecule, bool ribbon,
    const std::function<QVector3D(std::size_t)>& colorForAtom,
    std::vector<RibbonVertex>& vertices, std::vector<std::uint32_t>& indices) {
    const unsigned subdivisions = ribbon ? 8U : 10U;
    const auto frames = molecule.ribbonFrames(subdivisions);
    vertices.reserve(vertices.size() + frames.size() * sides);
    indices.reserve(indices.size() + frames.size() * sides * 6);
    for (std::size_t start = 0; start < frames.size();) {
        std::size_t end = start + 1;
        while (end < frames.size() && !frames[end].startsChain) ++end;
        appendStrip(std::span(frames).subspan(start, end - start), ribbon, subdivisions, colorForAtom, vertices, indices);
        start = end;
    }
}

} // namespace jlmol
