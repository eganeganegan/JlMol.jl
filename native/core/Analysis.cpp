#include "core/Analysis.h"

#include <algorithm>
#include <cmath>

namespace jlmol::analysis {
namespace {

constexpr double epsilon = 1.0e-12;
constexpr double radiansToDegrees = 180.0 / 3.14159265358979323846;

struct DoubleVec3 {
    double x;
    double y;
    double z;
};

DoubleVec3 subtract(const Vec3& first, const Vec3& second) noexcept {
    return {static_cast<double>(first.x) - second.x,
            static_cast<double>(first.y) - second.y,
            static_cast<double>(first.z) - second.z};
}

DoubleVec3 subtract(const DoubleVec3& first, const DoubleVec3& second) noexcept {
    return {first.x - second.x, first.y - second.y, first.z - second.z};
}

double dot(const DoubleVec3& first, const DoubleVec3& second) noexcept {
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

DoubleVec3 cross(const DoubleVec3& first, const DoubleVec3& second) noexcept {
    return {first.y * second.z - first.z * second.y,
            first.z * second.x - first.x * second.z,
            first.x * second.y - first.y * second.x};
}

double norm(const DoubleVec3& value) noexcept {
    return std::sqrt(dot(value, value));
}

DoubleVec3 scale(const DoubleVec3& value, double factor) noexcept {
    return {value.x * factor, value.y * factor, value.z * factor};
}

double atomicMass(const std::string& element) noexcept {
    if (element == "H") return 1.008;
    if (element == "C") return 12.011;
    if (element == "N") return 14.007;
    if (element == "O") return 15.999;
    if (element == "F") return 18.998403;
    if (element == "Na") return 22.989769;
    if (element == "Mg") return 24.305;
    if (element == "P") return 30.973762;
    if (element == "S") return 32.06;
    if (element == "Cl") return 35.45;
    if (element == "K") return 39.0983;
    if (element == "Ca") return 40.078;
    if (element == "Fe") return 55.845;
    if (element == "Zn") return 65.38;
    if (element == "Br") return 79.904;
    if (element == "I") return 126.90447;
    return 12.011;
}

} // namespace

double distance(const Vec3& first, const Vec3& second) noexcept {
    return norm(subtract(first, second));
}

std::optional<double> angleDegrees(const Vec3& first, const Vec3& vertex, const Vec3& third) noexcept {
    const auto firstVector = subtract(first, vertex);
    const auto secondVector = subtract(third, vertex);
    const auto denominator = norm(firstVector) * norm(secondVector);
    if (denominator <= epsilon) return std::nullopt;
    const auto cosine = std::clamp(dot(firstVector, secondVector) / denominator, -1.0, 1.0);
    return std::acos(cosine) * radiansToDegrees;
}

std::optional<double> dihedralDegrees(const Vec3& first, const Vec3& second,
                                      const Vec3& third, const Vec3& fourth) noexcept {
    const auto firstBond = subtract(second, first);
    const auto middleBond = subtract(third, second);
    const auto finalBond = subtract(fourth, third);
    const auto middleLength = norm(middleBond);
    const auto firstNormal = cross(firstBond, middleBond);
    const auto secondNormal = cross(middleBond, finalBond);
    const auto firstNormalLength = norm(firstNormal);
    const auto secondNormalLength = norm(secondNormal);
    if (middleLength <= epsilon || firstNormalLength <= epsilon || secondNormalLength <= epsilon) {
        return std::nullopt;
    }

    const auto unitMiddle = scale(middleBond, 1.0 / middleLength);
    const auto x = dot(firstNormal, secondNormal) / (firstNormalLength * secondNormalLength);
    const auto y = dot(cross(firstNormal, unitMiddle), secondNormal) /
                   (firstNormalLength * secondNormalLength);
    return std::atan2(y, x) * radiansToDegrees;
}

double geometricRadiusOfGyration(std::span<const Vec3> positions) noexcept {
    if (positions.empty()) return 0.0;
    DoubleVec3 center{};
    for (const auto& position : positions) {
        center.x += position.x;
        center.y += position.y;
        center.z += position.z;
    }
    const auto inverseCount = 1.0 / static_cast<double>(positions.size());
    center = scale(center, inverseCount);

    double squaredDistanceSum = 0.0;
    for (const auto& position : positions) {
        const DoubleVec3 point{position.x, position.y, position.z};
        const auto offset = subtract(point, center);
        squaredDistanceSum += dot(offset, offset);
    }
    return std::sqrt(squaredDistanceSum * inverseCount);
}

double radiusOfGyration(const Molecule& molecule) noexcept {
    if (molecule.atoms().empty()) return 0.0;
    DoubleVec3 centerOfMass{};
    double totalMass = 0.0;
    for (const auto& atom : molecule.atoms()) {
        const auto mass = atomicMass(atom.element);
        centerOfMass.x += mass * atom.position.x;
        centerOfMass.y += mass * atom.position.y;
        centerOfMass.z += mass * atom.position.z;
        totalMass += mass;
    }
    centerOfMass = scale(centerOfMass, 1.0 / totalMass);

    double weightedSquaredDistance = 0.0;
    for (const auto& atom : molecule.atoms()) {
        const DoubleVec3 point{atom.position.x, atom.position.y, atom.position.z};
        const auto offset = subtract(point, centerOfMass);
        weightedSquaredDistance += atomicMass(atom.element) * dot(offset, offset);
    }
    return std::sqrt(weightedSquaredDistance / totalMass);
}

} // namespace jlmol::analysis
