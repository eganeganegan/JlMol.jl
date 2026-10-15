#include "core/Molecule.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace jlmol {
namespace {

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    return first < last ? std::string(first, last) : std::string{};
}

std::string field(const std::string& line, std::size_t offset, std::size_t length) {
    if (offset >= line.size()) {
        return {};
    }
    return trim(line.substr(offset, std::min(length, line.size() - offset)));
}

float parseFloat(const std::string& line, std::size_t offset, std::size_t length, float fallback = 0.0F) {
    const auto value = field(line, offset, length);
    if (value.empty()) {
        return fallback;
    }
    try {
        return std::stof(value);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid numeric PDB field: '" + value + "'");
    }
}

int parseInt(const std::string& line, std::size_t offset, std::size_t length, int fallback = 0) {
    const auto value = field(line, offset, length);
    if (value.empty()) {
        return fallback;
    }
    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid integer PDB field: '" + value + "'");
    }
}

std::string normalizedElement(std::string element, const std::string& atomName) {
    if (element.empty()) {
        element = atomName;
        element.erase(std::remove_if(element.begin(), element.end(), [](unsigned char c) { return !std::isalpha(c); }), element.end());
        if (element.size() > 1 && std::islower(static_cast<unsigned char>(element[1])) == 0) {
            element.resize(1);
        } else if (element.size() > 2) {
            element.resize(2);
        }
    }
    if (!element.empty()) {
        element[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(element[0])));
    }
    if (element.size() > 1) {
        element[1] = static_cast<char>(std::tolower(static_cast<unsigned char>(element[1])));
    }
    return element.empty() ? "X" : element;
}

float covalentRadius(const std::string& element) {
    static const std::unordered_map<std::string, float> radii{
        {"H", 0.31F}, {"C", 0.76F}, {"N", 0.71F}, {"O", 0.66F}, {"F", 0.57F},
        {"P", 1.07F}, {"S", 1.05F}, {"Cl", 1.02F}, {"Fe", 1.32F}, {"Zn", 1.22F}};
    const auto it = radii.find(element);
    return it == radii.end() ? 0.77F : it->second;
}

struct Cell {
    int x;
    int y;
    int z;
    bool operator==(const Cell&) const = default;
};

struct CellHash {
    std::size_t operator()(const Cell& cell) const noexcept {
        auto h = static_cast<std::size_t>(cell.x) * 73856093U;
        h ^= static_cast<std::size_t>(cell.y) * 19349663U;
        h ^= static_cast<std::size_t>(cell.z) * 83492791U;
        return h;
    }
};

void inferBonds(Molecule& molecule, std::vector<Bond>& bonds, const std::vector<Atom>& atoms) {
    constexpr float cellSize = 2.4F;
    std::unordered_map<Cell, std::vector<std::uint32_t>, CellHash> grid;
    grid.reserve(atoms.size());
    auto cellFor = [](const Vec3& p) {
        return Cell{static_cast<int>(std::floor(p.x / cellSize)), static_cast<int>(std::floor(p.y / cellSize)),
                    static_cast<int>(std::floor(p.z / cellSize))};
    };
    for (std::uint32_t i = 0; i < atoms.size(); ++i) {
        const auto cell = cellFor(atoms[i].position);
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    const auto found = grid.find(Cell{cell.x + dx, cell.y + dy, cell.z + dz});
                    if (found == grid.end()) {
                        continue;
                    }
                    for (const auto j : found->second) {
                        const auto& a = atoms[i];
                        const auto& b = atoms[j];
                        const float x = a.position.x - b.position.x;
                        const float y = a.position.y - b.position.y;
                        const float z = a.position.z - b.position.z;
                        const float distanceSquared = x * x + y * y + z * z;
                        const float cutoff = covalentRadius(a.element) + covalentRadius(b.element) + 0.45F;
                        if (distanceSquared > 0.16F && distanceSquared <= cutoff * cutoff) {
                            bonds.push_back(Bond{j, i});
                        }
                    }
                }
            }
        }
        grid[cell].push_back(i);
    }
    (void)molecule;
}

} // namespace

Molecule Molecule::loadPdb(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Could not open structure: " + path.string());
    }

    Molecule molecule;
    molecule.sourcePath_ = path;
    molecule.name_ = path.stem().string();
    std::string line;
    int currentModel = 1;
    std::uint64_t nextId = 1;
    while (std::getline(input, line)) {
        const auto record = field(line, 0, 6);
        if (record == "MODEL") {
            currentModel = parseInt(line, 10, 4, currentModel + 1);
            continue;
        }
        if (record == "ENDMDL" && !molecule.atoms_.empty()) {
            break; // Initial milestone loads the first coordinate model only.
        }
        if (record != "ATOM" && record != "HETATM") {
            continue;
        }
        if (line.size() < 54) {
            throw std::runtime_error("Malformed PDB atom record (expected at least 54 columns)");
        }
        Atom atom;
        atom.id = nextId++;
        atom.serial = parseInt(line, 6, 5);
        atom.name = field(line, 12, 4);
        atom.alternateLocation = line.size() > 16 ? line[16] : ' ';
        if (atom.alternateLocation != ' ' && atom.alternateLocation != 'A') {
            continue;
        }
        atom.residueName = field(line, 17, 3);
        atom.chainId = field(line, 21, 1);
        atom.residueId = parseInt(line, 22, 4);
        atom.position = Vec3{parseFloat(line, 30, 8), parseFloat(line, 38, 8), parseFloat(line, 46, 8)};
        atom.occupancy = parseFloat(line, 54, 6, 1.0F);
        atom.bFactor = parseFloat(line, 60, 6);
        atom.element = normalizedElement(field(line, 76, 2), atom.name);
        atom.modelId = currentModel;
        molecule.atoms_.push_back(std::move(atom));
    }
    if (molecule.atoms_.empty()) {
        throw std::runtime_error("No atoms were found in PDB file: " + path.string());
    }
    inferBonds(molecule, molecule.bonds_, molecule.atoms_);
    return molecule;
}

Vec3 Molecule::center() const noexcept {
    Vec3 result{};
    if (atoms_.empty()) {
        return result;
    }
    for (const auto& atom : atoms_) {
        result.x += atom.position.x;
        result.y += atom.position.y;
        result.z += atom.position.z;
    }
    const auto scale = 1.0F / static_cast<float>(atoms_.size());
    return Vec3{result.x * scale, result.y * scale, result.z * scale};
}

float Molecule::radius() const noexcept {
    const auto c = center();
    float radiusSquared = 1.0F;
    for (const auto& atom : atoms_) {
        const auto x = atom.position.x - c.x;
        const auto y = atom.position.y - c.y;
        const auto z = atom.position.z - c.z;
        radiusSquared = std::max(radiusSquared, x * x + y * y + z * z);
    }
    return std::sqrt(radiusSquared);
}

std::array<float, 3> elementColor(const std::string& element) noexcept {
    if (element == "H") return {0.92F, 0.92F, 0.92F};
    if (element == "C") return {0.24F, 0.30F, 0.38F};
    if (element == "N") return {0.18F, 0.38F, 0.95F};
    if (element == "O") return {0.95F, 0.18F, 0.16F};
    if (element == "S") return {0.95F, 0.78F, 0.12F};
    if (element == "P") return {1.00F, 0.48F, 0.10F};
    if (element == "Cl") return {0.12F, 0.80F, 0.25F};
    return {0.66F, 0.44F, 0.75F};
}

float elementRadius(const std::string& element) noexcept {
    if (element == "H") return 1.20F;
    if (element == "C") return 1.70F;
    if (element == "N") return 1.55F;
    if (element == "O") return 1.52F;
    if (element == "S") return 1.80F;
    if (element == "P") return 1.80F;
    return 1.60F;
}

} // namespace jlmol

