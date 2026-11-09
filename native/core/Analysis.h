#pragma once

#include "core/Molecule.h"

#include <optional>
#include <span>

namespace jlmol::analysis {

[[nodiscard]] double distance(const Vec3& first, const Vec3& second) noexcept;
[[nodiscard]] std::optional<double> angleDegrees(const Vec3& first, const Vec3& vertex,
                                                 const Vec3& third) noexcept;
[[nodiscard]] std::optional<double> dihedralDegrees(const Vec3& first, const Vec3& second,
                                                    const Vec3& third, const Vec3& fourth) noexcept;
[[nodiscard]] double geometricRadiusOfGyration(std::span<const Vec3> positions) noexcept;
[[nodiscard]] double radiusOfGyration(const Molecule& molecule) noexcept;

} // namespace jlmol::analysis
