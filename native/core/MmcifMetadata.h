#pragma once

#include "core/Molecule.h"

namespace jlmol {

struct MmcifMetadata {
    struct Range {
        std::string chain;
        int first;
        int last;
        SecondaryStructure structure;
    };
    bool hasMacromolecularAtoms{};
    std::vector<Range> ranges;

    void annotate(std::vector<Atom>& atoms) const;
};

// Reads metadata only. Chemfiles remains responsible for coordinates and topology.
MmcifMetadata readMmcifMetadata(const std::filesystem::path& path);

} // namespace jlmol
