#include "bridge/JlMolAPI.h"
#include "core/MolecularScene.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
    assert(argc == 3);
    const auto molecule = std::make_shared<jlmol::Molecule>(jlmol::Molecule::loadPdb(std::filesystem::path(argv[1])));
    assert(molecule->atoms().size() == 10);
    assert(!molecule->bonds().empty());
    assert(molecule->atoms().front().element == "N");
    assert(molecule->atoms().back().chainId == "B");
    assert(molecule->radius() > 1.0F);

    jlmol::MolecularScene scene;
    scene.setMolecule(molecule);
    jlmol_set_active_scene(&scene);
    assert(jlmol_api_version() == JLMOL_API_VERSION);
    assert(jlmol_active_atom_count() == 10);
    assert(jlmol_set_active_representation("spacefill") == 0);
    assert(scene.representation() == jlmol::Representation::SpaceFilling);
    assert(jlmol_set_active_color_scheme("chain") == 0);
    assert(scene.colorScheme() == jlmol::ColorScheme::Chain);
    scene.selectAtom(molecule->atoms().front().id);
    assert(jlmol_has_selection() == 1);
    assert(std::string(jlmol_selected_atom_name()) == "N");
    assert(jlmol_selected_atom_residue_id() == 1);
    jlmol_set_active_scene(nullptr);

    const auto protein = jlmol::Molecule::loadPdb(std::filesystem::path(argv[2]));
    assert(protein.atoms().size() == 327);
    assert(protein.bonds().size() > 300);
    assert(protein.radius() > 10.0F);

    std::cout << "native core tests passed\n";
    return 0;
}

