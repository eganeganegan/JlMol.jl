#include "core/Molecule.h"
#include "core/MmcifMetadata.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>

#if defined(JLMOL_HAS_CHEMFILES)
#include <chemfiles.hpp>
#endif

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
                            bonds.push_back(Bond{j, i, 1});
                        }
                    }
                }
            }
        }
        grid[cell].push_back(i);
    }
    (void)molecule;
}

struct SecondaryRange {
    SecondaryStructure structure;
    std::string startChain;
    int startResidue;
    std::string endChain;
    int endResidue;
};

void annotatePdbSecondaryStructure(const std::filesystem::path& path, std::vector<Atom>& atoms) {
    const auto extension = path.extension().string();
    if (extension != ".pdb" && extension != ".PDB" && extension != ".ent" && extension != ".ENT") return;
    std::ifstream input(path);
    if (!input) return;
    std::vector<SecondaryRange> ranges;
    std::string line;
    while (std::getline(input, line)) {
        const auto record = field(line, 0, 6);
        if (record == "HELIX") {
            ranges.push_back({SecondaryStructure::Helix, field(line, 19, 1), parseInt(line, 21, 4),
                              field(line, 31, 1), parseInt(line, 33, 4)});
        } else if (record == "SHEET") {
            ranges.push_back({SecondaryStructure::Sheet, field(line, 21, 1), parseInt(line, 22, 4),
                              field(line, 32, 1), parseInt(line, 33, 4)});
        }
    }
    for (auto& atom : atoms) {
        for (const auto& range : ranges) {
            if (range.startChain == range.endChain && atom.chainId == range.startChain &&
                atom.residueId >= range.startResidue && atom.residueId <= range.endResidue) {
                atom.secondaryStructure = range.structure;
                break;
            }
        }
    }
}

} // namespace

Molecule Molecule::load(const std::filesystem::path& path) {
#if defined(JLMOL_HAS_CHEMFILES)
    try {
        auto extension = path.extension().string();
        for (auto& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const auto cif = (extension == ".cif" || extension == ".mmcif") ? readMmcifMetadata(path) : MmcifMetadata{};
        // .cif is also used for small-molecule CIF. Select the protein reader
        // only when macromolecular atom columns are present.
        chemfiles::Trajectory trajectory(path.string(), 'r',
            cif.hasMacromolecularAtoms || extension == ".mmcif" ? "mmCIF" : "");
        auto frame = trajectory.read();
        if (frame.size() == 0) {
            throw std::runtime_error("No atoms were found in structure: " + path.string());
        }

        Molecule molecule;
        molecule.sourcePath_ = std::filesystem::absolute(path);
        molecule.name_ = path.stem().string();
        molecule.atoms_.reserve(frame.size());
        const auto& positions = frame.positions();
        const auto& topology = frame.topology();
        for (std::size_t index = 0; index < frame.size(); ++index) {
            const auto& sourceAtom = frame[index];
            Atom atom;
            atom.id = index + 1;
            atom.serial = static_cast<int>(index + 1);
            atom.name = sourceAtom.name();
            atom.element = normalizedElement(sourceAtom.type(), sourceAtom.name());
            atom.position = Vec3{static_cast<float>(positions[index][0]), static_cast<float>(positions[index][1]),
                                 static_cast<float>(positions[index][2])};
            const auto residue = topology.residue_for_atom(index);
            if (residue) {
                atom.residueName = residue->name();
                atom.residueId = static_cast<int>(residue->id().value_or(0));
                atom.chainId = residue->get<chemfiles::Property::STRING>("chainid").value_or("");
            }
            const auto altloc = sourceAtom.get<chemfiles::Property::STRING>("altloc").value_or("");
            atom.alternateLocation = altloc.empty() ? ' ' : altloc.front();
            atom.occupancy = static_cast<float>(sourceAtom.get<chemfiles::Property::DOUBLE>("occupancy").value_or(1.0));
            atom.bFactor = static_cast<float>(sourceAtom.get<chemfiles::Property::DOUBLE>("b_factor").value_or(0.0));
            molecule.atoms_.push_back(std::move(atom));
        }

        const auto appendTopologyBonds = [&molecule](const chemfiles::Topology& sourceTopology) {
            const auto& sourceBonds = sourceTopology.bonds();
            const auto& orders = sourceTopology.bond_orders();
            molecule.bonds_.reserve(sourceBonds.size());
            for (std::size_t index = 0; index < sourceBonds.size(); ++index) {
                const int order = index < orders.size() ? static_cast<int>(orders[index]) : 1;
                molecule.bonds_.push_back(Bond{static_cast<std::uint32_t>(sourceBonds[index][0]),
                                               static_cast<std::uint32_t>(sourceBonds[index][1]), order});
            }
        };
        appendTopologyBonds(topology);
        if (molecule.bonds_.empty()) {
            try {
                frame.guess_bonds();
                appendTopologyBonds(frame.topology());
            } catch (const chemfiles::Error&) {
                inferBonds(molecule, molecule.bonds_, molecule.atoms_);
            }
        }
        annotatePdbSecondaryStructure(path, molecule.atoms_);
        cif.annotate(molecule.atoms_);
        return molecule;
    } catch (const chemfiles::Error& error) {
        throw std::runtime_error("Could not read '" + path.string() + "': " + error.what());
    }
#else
    const auto extension = path.extension().string();
    if (extension == ".pdb" || extension == ".PDB") {
        return loadPdb(path);
    }
    throw std::runtime_error("This build only supports PDB files; rebuild with Chemfiles for multi-format import");
#endif
}

Molecule Molecule::loadPdb(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Could not open structure: " + path.string());
    }

    Molecule molecule;
    molecule.sourcePath_ = std::filesystem::absolute(path);
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
    annotatePdbSecondaryStructure(path, molecule.atoms_);
    return molecule;
}

Molecule Molecule::withPositions(std::span<const Vec3> positions) const {
    if (positions.size() != atoms_.size()) throw std::invalid_argument("Coordinate count does not match topology");
    auto result = *this;
    for (std::size_t i = 0; i < positions.size(); ++i) result.atoms_[i].position = positions[i];
    return result;
}

const Atom* Molecule::atom(std::uint64_t id) const noexcept {
    if (!id || atoms_.empty() || id > atoms_.back().id) return nullptr;
    if (id <= atoms_.size() && atoms_[id - 1].id == id) return &atoms_[id - 1];
    // The PDB fallback can skip alternate locations, leaving gaps in its ordered IDs.
    const auto found = std::lower_bound(atoms_.begin(), atoms_.end(), id,
        [](const Atom& atom, std::uint64_t target) { return atom.id < target; });
    return found != atoms_.end() && found->id == id ? &*found : nullptr;
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

std::vector<Bond> Molecule::cAlphaTrace() const {
    std::vector<Bond> trace;
    std::optional<std::uint32_t> previous;
    for (std::uint32_t index = 0; index < atoms_.size(); ++index) {
        const auto& atom = atoms_[index];
        if (atom.name != "CA") continue;
        if (previous) {
            const auto& prior = atoms_[*previous];
            const float dx = atom.position.x - prior.position.x;
            const float dy = atom.position.y - prior.position.y;
            const float dz = atom.position.z - prior.position.z;
            if (prior.chainId == atom.chainId && dx * dx + dy * dy + dz * dz <= 20.25F) {
                trace.push_back(Bond{*previous, index, 1});
            }
        }
        previous = index;
    }
    return trace;
}

std::vector<Bond> Molecule::backboneTrace() const {
    std::vector<Bond> trace;
    std::optional<std::uint32_t> previous;
    for (std::uint32_t index = 0; index < atoms_.size(); ++index) {
        const auto& atom = atoms_[index];
        if (atom.name != "N" && atom.name != "CA" && atom.name != "C") continue;
        if (previous) {
            const auto& prior = atoms_[*previous];
            const float dx = atom.position.x - prior.position.x;
            const float dy = atom.position.y - prior.position.y;
            const float dz = atom.position.z - prior.position.z;
            if (prior.chainId == atom.chainId && dx * dx + dy * dy + dz * dz <= 4.0F) {
                trace.push_back(Bond{*previous, index, 1});
            }
        }
        previous = index;
    }
    return trace;
}

std::vector<TraceSegment> Molecule::smoothCAlphaTrace(unsigned subdivisions) const {
    subdivisions = std::max(1U, subdivisions);
    std::vector<std::vector<const Atom*>> chains;
    for (const auto& atom : atoms_) {
        if (atom.name != "CA") continue;
        bool newSegment = chains.empty() || chains.back().empty();
        if (!newSegment) {
            const auto* previous = chains.back().back();
            const float dx = atom.position.x - previous->position.x;
            const float dy = atom.position.y - previous->position.y;
            const float dz = atom.position.z - previous->position.z;
            newSegment = atom.chainId != previous->chainId || dx * dx + dy * dy + dz * dz > 20.25F;
        }
        if (newSegment) chains.emplace_back();
        chains.back().push_back(&atom);
    }

    const auto interpolate = [](const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t) {
        const float t2 = t * t;
        const float t3 = t2 * t;
        const auto component = [t, t2, t3](float a, float b, float c, float d) {
            return 0.5F * ((2.0F * b) + (-a + c) * t + (2.0F * a - 5.0F * b + 4.0F * c - d) * t2 +
                           (-a + 3.0F * b - 3.0F * c + d) * t3);
        };
        return Vec3{component(p0.x, p1.x, p2.x, p3.x), component(p0.y, p1.y, p2.y, p3.y),
                    component(p0.z, p1.z, p2.z, p3.z)};
    };

    std::vector<TraceSegment> result;
    for (const auto& chain : chains) {
        if (chain.size() < 2) continue;
        result.reserve(result.size() + (chain.size() - 1) * subdivisions);
        for (std::size_t index = 0; index + 1 < chain.size(); ++index) {
            const auto& p0 = chain[index == 0 ? index : index - 1]->position;
            const auto& p1 = chain[index]->position;
            const auto& p2 = chain[index + 1]->position;
            const auto& p3 = chain[index + 2 < chain.size() ? index + 2 : index + 1]->position;
            Vec3 previous = p1;
            for (unsigned step = 1; step <= subdivisions; ++step) {
                const auto next = interpolate(p0, p1, p2, p3, static_cast<float>(step) / static_cast<float>(subdivisions));
                result.push_back(TraceSegment{previous, next, chain[index]->secondaryStructure,
                                              static_cast<std::size_t>(chain[index] - atoms_.data()),
                                              index == 0 && step == 1});
                previous = next;
            }
        }
    }
    return result;
}

std::vector<RibbonFrame> Molecule::ribbonFrames(unsigned subdivisions) const {
    const auto trace = smoothCAlphaTrace(subdivisions);
    std::vector<RibbonFrame> frames;
    frames.reserve(trace.size());
    Vec3 previousSide{};
    Vec3 previousEnd{};
    bool hasPrevious = false;
    const auto dot = [](const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    const auto cross = [](const Vec3& a, const Vec3& b) {
        return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    };
    const auto normalized = [&dot](Vec3 value) {
        const float length = std::sqrt(dot(value, value));
        if (length <= 1.0e-6F) return Vec3{1.0F, 0.0F, 0.0F};
        return Vec3{value.x / length, value.y / length, value.z / length};
    };
    for (const auto& segment : trace) {
        const Vec3 tangent = normalized(Vec3{segment.second.x - segment.first.x, segment.second.y - segment.first.y,
                                             segment.second.z - segment.first.z});
        const float joinDx = segment.first.x - previousEnd.x;
        const float joinDy = segment.first.y - previousEnd.y;
        const float joinDz = segment.first.z - previousEnd.z;
        const bool contiguous = hasPrevious && !segment.startsChain &&
            joinDx * joinDx + joinDy * joinDy + joinDz * joinDz < 1.0e-5F;
        Vec3 side;
        if (contiguous) {
            const float along = dot(previousSide, tangent);
            side = normalized(Vec3{previousSide.x - tangent.x * along, previousSide.y - tangent.y * along,
                                   previousSide.z - tangent.z * along});
        } else {
            const Vec3 reference = std::abs(tangent.z) < 0.9F ? Vec3{0.0F, 0.0F, 1.0F} : Vec3{0.0F, 1.0F, 0.0F};
            side = normalized(cross(tangent, reference));
        }
        const Vec3 normal = normalized(cross(side, tangent));
        frames.push_back(RibbonFrame{segment.first, segment.second, side, normal, segment.secondaryStructure,
                                     segment.sourceAtom, segment.startsChain});
        previousSide = side;
        previousEnd = segment.second;
        hasPrevious = true;
    }
    return frames;
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

std::array<float, 3> secondaryStructureColor(SecondaryStructure structure) noexcept {
    switch (structure) {
    case SecondaryStructure::Helix: return {0.92F, 0.24F, 0.30F};
    case SecondaryStructure::Sheet: return {0.95F, 0.78F, 0.18F};
    case SecondaryStructure::Coil: return {0.32F, 0.72F, 0.88F};
    }
    return {0.7F, 0.7F, 0.7F};
}

std::string secondaryStructureName(SecondaryStructure structure) {
    switch (structure) {
    case SecondaryStructure::Helix: return "helix";
    case SecondaryStructure::Sheet: return "sheet";
    case SecondaryStructure::Coil: return "coil";
    }
    return "coil";
}

} // namespace jlmol

