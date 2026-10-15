#pragma once

#include "core/Molecule.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace jlmol {

enum class Representation { BallAndStick, SpaceFilling, Points, Lines };
enum class ColorScheme { Element, Chain, Residue };

class MolecularScene {
public:
    void setMolecule(std::shared_ptr<Molecule> molecule);
    [[nodiscard]] std::shared_ptr<const Molecule> molecule() const;

    void setRepresentation(Representation representation);
    [[nodiscard]] Representation representation() const;
    void setColorScheme(ColorScheme scheme);
    [[nodiscard]] ColorScheme colorScheme() const;

    void selectAtom(std::optional<std::uint64_t> atomId);
    [[nodiscard]] std::optional<std::uint64_t> selectedAtomId() const;
    [[nodiscard]] std::optional<Atom> selectedAtom() const;
    [[nodiscard]] std::uint64_t generation() const;

private:
    mutable std::mutex mutex_;
    std::shared_ptr<Molecule> molecule_;
    Representation representation_{Representation::BallAndStick};
    ColorScheme colorScheme_{ColorScheme::Element};
    std::optional<std::uint64_t> selectedAtomId_;
    std::uint64_t generation_{};
};

[[nodiscard]] std::string toString(Representation representation);
[[nodiscard]] std::optional<Representation> representationFromString(const std::string& value);
[[nodiscard]] std::string toString(ColorScheme scheme);
[[nodiscard]] std::optional<ColorScheme> colorSchemeFromString(const std::string& value);

} // namespace jlmol

