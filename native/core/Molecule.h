#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace jlmol {

struct Vec3 {
    float x{};
    float y{};
    float z{};
};

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
};

struct Bond {
    std::uint32_t first{};
    std::uint32_t second{};
};

class Molecule {
public:
    static Molecule loadPdb(const std::filesystem::path& path);

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::filesystem::path& sourcePath() const noexcept { return sourcePath_; }
    [[nodiscard]] const std::vector<Atom>& atoms() const noexcept { return atoms_; }
    [[nodiscard]] const std::vector<Bond>& bonds() const noexcept { return bonds_; }
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

} // namespace jlmol

