#pragma once

#include "core/Molecule.h"
#include "renderer/RenderTypes.h"

#include <functional>

namespace jlmol {

// Appends an indexed tube/ribbon mesh. Each connected chain has shared rings
// and exactly two end caps. The color callback takes a molecule atom index.
void appendCartoonGeometry(const Molecule& molecule, bool ribbon,
    const std::function<QVector3D(std::size_t)>& colorForAtom,
    std::vector<RibbonVertex>& vertices, std::vector<std::uint32_t>& indices);

} // namespace jlmol
