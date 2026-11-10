#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace jlmol {

struct Vec3 {
    float x{};
    float y{};
    float z{};
};

enum class SecondaryStructure { Coil, Helix, Sheet };

struct Atom {
    std::uint64_t id{};
    int serial{};
    std::string name;
    std::string element;
    std::string residueName;
    int residueId{};
    std::string chainId;
    Vec3 position;
    float occupancy{1.0F};
    float bFactor{};
    char alternateLocation{' '};
    int modelId{1};
    SecondaryStructure secondaryStructure{SecondaryStructure::Coil};
};

struct Bond {
    std::uint32_t first{};
    std::uint32_t second{};
    int order{1};
};

struct TraceSegment {
    Vec3 first;
    Vec3 second;
    SecondaryStructure secondaryStructure{SecondaryStructure::Coil};
    std::size_t sourceAtom{};
};

struct RibbonFrame {
    Vec3 first;
    Vec3 second;
    Vec3 side;
    Vec3 normal;
    SecondaryStructure secondaryStructure{SecondaryStructure::Coil};
    std::size_t sourceAtom{};
};

class Molecule {
public:
    static Molecule load(const std::filesystem::path& path);
    static Molecule loadPdb(const std::filesystem::path& path);
    [[nodiscard]] Molecule withPositions(std::span<const Vec3> positions) const;

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::filesystem::path& sourcePath() const noexcept { return sourcePath_; }
    [[nodiscard]] const std::vector<Atom>& atoms() const noexcept { return atoms_; }
    [[nodiscard]] const std::vector<Bond>& bonds() const noexcept { return bonds_; }
    [[nodiscard]] std::vector<Bond> cAlphaTrace() const;
    [[nodiscard]] std::vector<Bond> backboneTrace() const;
    [[nodiscard]] std::vector<TraceSegment> smoothCAlphaTrace(unsigned subdivisions = 6) const;
    [[nodiscard]] std::vector<RibbonFrame> ribbonFrames(unsigned subdivisions = 6) const;
    [[nodiscard]] Vec3 center() const noexcept;
    [[nodiscard]] float radius() const noexcept;

private:
    std::string name_;
    std::filesystem::path sourcePath_;
    std::vector<Atom> atoms_;
    std::vector<Bond> bonds_;
};

[[nodiscard]] std::array<float, 3> elementColor(const std::string& element) noexcept;
[[nodiscard]] float elementRadius(const std::string& element) noexcept;
[[nodiscard]] std::array<float, 3> secondaryStructureColor(SecondaryStructure structure) noexcept;
[[nodiscard]] std::string secondaryStructureName(SecondaryStructure structure);

} // namespace jlmol

