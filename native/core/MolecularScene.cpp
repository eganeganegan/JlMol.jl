#include "core/MolecularScene.h"

#include <algorithm>
#include <cctype>

namespace jlmol {

void MolecularScene::setMolecule(std::shared_ptr<Molecule> molecule) {
    std::scoped_lock lock(mutex_);
    molecule_ = std::move(molecule);
    selectedAtomId_.reset();
    ++generation_;
}

std::shared_ptr<const Molecule> MolecularScene::molecule() const {
    std::scoped_lock lock(mutex_);
    return molecule_;
}

void MolecularScene::setRepresentation(Representation representation) {
    std::scoped_lock lock(mutex_);
    representation_ = representation;
    ++generation_;
}

Representation MolecularScene::representation() const {
    std::scoped_lock lock(mutex_);
    return representation_;
}

void MolecularScene::setColorScheme(ColorScheme scheme) {
    std::scoped_lock lock(mutex_);
    colorScheme_ = scheme;
    ++generation_;
}

ColorScheme MolecularScene::colorScheme() const {
    std::scoped_lock lock(mutex_);
    return colorScheme_;
}

void MolecularScene::selectAtom(std::optional<std::uint64_t> atomId) {
    std::scoped_lock lock(mutex_);
    selectedAtomId_ = atomId;
    ++generation_;
}

std::optional<std::uint64_t> MolecularScene::selectedAtomId() const {
    std::scoped_lock lock(mutex_);
    return selectedAtomId_;
}

std::optional<Atom> MolecularScene::selectedAtom() const {
    std::scoped_lock lock(mutex_);
    if (!molecule_ || !selectedAtomId_) {
        return std::nullopt;
    }
    const auto found = std::find_if(molecule_->atoms().begin(), molecule_->atoms().end(),
                                    [this](const Atom& atom) { return atom.id == *selectedAtomId_; });
    return found == molecule_->atoms().end() ? std::nullopt : std::optional<Atom>{*found};
}

std::uint64_t MolecularScene::generation() const {
    std::scoped_lock lock(mutex_);
    return generation_;
}

std::string toString(Representation representation) {
    switch (representation) {
    case Representation::BallAndStick: return "ball_and_stick";
    case Representation::SpaceFilling: return "spacefill";
    case Representation::Points: return "points";
    case Representation::Lines: return "lines";
    }
    return "unknown";
}

std::optional<Representation> representationFromString(const std::string& value) {
    std::string key = value;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return c == '-' ? '_' : static_cast<char>(std::tolower(c));
    });
    if (key == "ball_and_stick" || key == "ballstick") return Representation::BallAndStick;
    if (key == "spacefill" || key == "space_filling" || key == "cpk") return Representation::SpaceFilling;
    if (key == "points") return Representation::Points;
    if (key == "lines") return Representation::Lines;
    return std::nullopt;
}

std::string toString(ColorScheme scheme) {
    switch (scheme) {
    case ColorScheme::Element: return "element";
    case ColorScheme::Chain: return "chain";
    case ColorScheme::Residue: return "residue";
    }
    return "unknown";
}

std::optional<ColorScheme> colorSchemeFromString(const std::string& value) {
    std::string key = value;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (key == "element" || key == "cpk") return ColorScheme::Element;
    if (key == "chain") return ColorScheme::Chain;
    if (key == "residue") return ColorScheme::Residue;
    return std::nullopt;
}

} // namespace jlmol
