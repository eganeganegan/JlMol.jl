#pragma once

#include "core/Trajectory.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace jlmol {

enum class Representation { BallAndStick, SpaceFilling, Points, Lines, CAlphaTrace, Backbone, Cartoon, Ribbon };
enum class ColorScheme { Element, Chain, Residue, SecondaryStructure };
using ObjectId = std::uint64_t;

struct MolecularObject {
    ObjectId id{};
    std::shared_ptr<Molecule> molecule;
    bool visible{true};
    Representation representation{Representation::BallAndStick};
    ColorScheme colorScheme{ColorScheme::Element};
    double opacity{1.0};
    std::shared_ptr<const Trajectory> trajectory;
    std::size_t frameIndex{};
    FrameMetadata frameMetadata;
};

class MolecularScene {
public:
    // Compatibility helper: replace the scene with one molecule.
    void setMolecule(std::shared_ptr<Molecule> molecule);
    [[nodiscard]] ObjectId addMolecule(std::shared_ptr<Molecule> molecule);
    [[nodiscard]] bool closeObject(ObjectId id);
    void attachTrajectory(ObjectId id, const std::filesystem::path& path);
    void setTrajectoryFrame(ObjectId id, std::size_t index);
    [[nodiscard]] std::vector<MolecularObject> objects() const;
    [[nodiscard]] std::optional<MolecularObject> object(ObjectId id) const;
    [[nodiscard]] std::size_t objectCount() const;

    [[nodiscard]] bool setActiveObject(ObjectId id);
    [[nodiscard]] std::optional<ObjectId> activeObjectId() const;
    [[nodiscard]] std::shared_ptr<const Molecule> molecule() const;

    [[nodiscard]] bool setObjectVisible(ObjectId id, bool visible);
    [[nodiscard]] bool objectVisible(ObjectId id) const;
    [[nodiscard]] bool setObjectOpacity(ObjectId id, double opacity);
    [[nodiscard]] std::optional<double> objectOpacity(ObjectId id) const;

    void setRepresentation(Representation representation);
    [[nodiscard]] bool setRepresentation(ObjectId id, Representation representation);
    [[nodiscard]] Representation representation() const;
    [[nodiscard]] std::optional<Representation> representation(ObjectId id) const;
    void setColorScheme(ColorScheme scheme);
    [[nodiscard]] bool setColorScheme(ObjectId id, ColorScheme scheme);
    [[nodiscard]] ColorScheme colorScheme() const;
    [[nodiscard]] std::optional<ColorScheme> colorScheme(ObjectId id) const;

    void selectAtom(std::optional<std::uint64_t> atomId);
    void selectAtom(ObjectId objectId, std::optional<std::uint64_t> atomId);
    void extendAtomSelection(ObjectId objectId, std::uint64_t atomId);
    void selectAtoms(ObjectId objectId, std::vector<std::uint64_t> atomIds);
    [[nodiscard]] std::optional<ObjectId> selectedObjectId() const;
    [[nodiscard]] std::optional<std::uint64_t> selectedAtomId() const;
    [[nodiscard]] std::vector<std::uint64_t> selectedAtomIds() const;
    [[nodiscard]] std::optional<Atom> selectedAtom() const;
    [[nodiscard]] std::vector<Atom> selectedAtoms() const;
    [[nodiscard]] std::uint64_t generation() const;

private:
    [[nodiscard]] auto findObject(ObjectId id) { return std::find_if(objects_.begin(), objects_.end(), [id](const auto& object) { return object.id == id; }); }
    [[nodiscard]] auto findObject(ObjectId id) const { return std::find_if(objects_.begin(), objects_.end(), [id](const auto& object) { return object.id == id; }); }

    mutable std::mutex mutex_;
    std::vector<MolecularObject> objects_;
    ObjectId nextObjectId_{1};
    std::optional<ObjectId> activeObjectId_;
    Representation defaultRepresentation_{Representation::BallAndStick};
    ColorScheme defaultColorScheme_{ColorScheme::Element};
    std::optional<ObjectId> selectedObjectId_;
    std::vector<std::uint64_t> selectedAtomIds_;
    std::uint64_t generation_{};
};

[[nodiscard]] std::string toString(Representation representation);
[[nodiscard]] std::optional<Representation> representationFromString(const std::string& value);
[[nodiscard]] std::string toString(ColorScheme scheme);
[[nodiscard]] std::optional<ColorScheme> colorSchemeFromString(const std::string& value);

} // namespace jlmol
