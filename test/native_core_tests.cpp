#include "bridge/JlMolAPI.h"
#include "core/Analysis.h"
#include "core/MolecularScene.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>

int main(int argc, char** argv) {
    assert(argc == 6);
    const jlmol::Vec3 origin{};
    const jlmol::Vec3 unitX{1.0F, 0.0F, 0.0F};
    const jlmol::Vec3 unitY{0.0F, 1.0F, 0.0F};
    const jlmol::Vec3 unitYZ{0.0F, 1.0F, 1.0F};
    assert(std::abs(jlmol::analysis::distance(origin, unitX) - 1.0) < 1.0e-12);
    assert(std::abs(*jlmol::analysis::angleDegrees(unitX, origin, unitY) - 90.0) < 1.0e-12);
    assert(std::abs(*jlmol::analysis::dihedralDegrees(unitX, origin, unitY, unitYZ) - 90.0) < 1.0e-12);
    const std::array positions{jlmol::Vec3{-1.0F, 0.0F, 0.0F}, jlmol::Vec3{1.0F, 0.0F, 0.0F}};
    assert(std::abs(jlmol::analysis::geometricRadiusOfGyration(positions) - 1.0) < 1.0e-12);
    const auto molecule = std::make_shared<jlmol::Molecule>(jlmol::Molecule::loadPdb(std::filesystem::path(argv[1])));
    assert(molecule->atoms().size() == 10);
    assert(!molecule->bonds().empty());
    assert(molecule->atoms().front().element == "N");
    assert(molecule->atoms().back().chainId == "B");
    assert(molecule->radius() > 1.0F);
    assert(molecule->cAlphaTrace().size() == 1);
    assert(molecule->backboneTrace().size() == 5);
    assert(molecule->smoothCAlphaTrace().size() == 6);
    assert(jlmol::representationFromString("c-alpha-trace") == jlmol::Representation::CAlphaTrace);
    assert(jlmol::representationFromString("backbone") == jlmol::Representation::Backbone);
    assert(jlmol::representationFromString("cartoon") == jlmol::Representation::Cartoon);
    assert(jlmol::colorSchemeFromString("secondary-structure") == jlmol::ColorScheme::SecondaryStructure);

    jlmol::MolecularScene scene;
    scene.setMolecule(molecule);
    jlmol_set_active_scene(&scene);
    assert(jlmol_api_version() == JLMOL_API_VERSION);
    assert(jlmol_active_atom_count() == 10);
    const auto firstObject = jlmol_active_object_id();
    assert(firstObject != 0);
    std::vector<double> coordinates(molecule->atoms().size() * 3);
    std::vector<uint64_t> atomIds(molecule->atoms().size());
    assert(jlmol_copy_object_atoms(firstObject, coordinates.data(), atomIds.data(), atomIds.size()) == 0);
    assert(atomIds.front() == molecule->atoms().front().id);
    assert(coordinates.front() == molecule->atoms().front().position.x);
    assert(std::string(jlmol_object_atom_field(firstObject, 0, "name")) == "N");
    const auto firstIdentity = std::string(jlmol_object_atom_field(firstObject, 0, "identity"));
    assert(!firstIdentity.empty());
    assert(firstIdentity != std::string(jlmol_object_atom_field(firstObject, 1, "identity")));
    assert(jlmol_copy_object_atoms(firstObject, coordinates.data(), atomIds.data(), 1) != 0);
    assert(jlmol_copy_object_atoms(firstObject, nullptr, atomIds.data(), atomIds.size()) != 0);
    assert(jlmol_copy_object_atoms(UINT64_MAX, coordinates.data(), atomIds.data(), atomIds.size()) != 0);
    assert(jlmol_object_count() == 1);
    assert(jlmol_object_id_at(0) == firstObject);
    assert(std::string(jlmol_object_name(firstObject)) == "mini");
    assert(jlmol_set_active_representation("spacefill") == 0);
    assert(scene.representation() == jlmol::Representation::SpaceFilling);
    assert(jlmol_set_active_color_scheme("chain") == 0);
    assert(scene.colorScheme() == jlmol::ColorScheme::Chain);
    scene.selectAtom(molecule->atoms().front().id);
    assert(jlmol_has_selection() == 1);
    assert(std::string(jlmol_selected_atom_name()) == "N");
    assert(jlmol_selected_atom_residue_id() == 1);
    double measurement = 0.0;
    assert(jlmol_measure_distance(firstObject, molecule->atoms()[0].id, molecule->atoms()[1].id, &measurement) == 0);
    assert(std::abs(measurement - jlmol::analysis::distance(molecule->atoms()[0].position,
                                                           molecule->atoms()[1].position)) < 1.0e-12);
    assert(jlmol_measure_angle(firstObject, molecule->atoms()[0].id, molecule->atoms()[1].id,
                               molecule->atoms()[2].id, &measurement) == 0);
    assert(std::abs(measurement - *jlmol::analysis::angleDegrees(molecule->atoms()[0].position,
                                                                 molecule->atoms()[1].position,
                                                                 molecule->atoms()[2].position)) < 1.0e-12);
    assert(jlmol_measure_dihedral(firstObject, molecule->atoms()[0].id, molecule->atoms()[1].id,
                                  molecule->atoms()[2].id, molecule->atoms()[7].id, &measurement) == 0);
    assert(std::abs(measurement - *jlmol::analysis::dihedralDegrees(molecule->atoms()[0].position,
                                                                    molecule->atoms()[1].position,
                                                                    molecule->atoms()[2].position,
                                                                    molecule->atoms()[7].position)) < 1.0e-12);
    assert(jlmol_radius_of_gyration(firstObject, &measurement) == 0);
    assert(measurement > 0.0);

    const auto protein = jlmol::Molecule::load(std::filesystem::path(argv[2]));
    assert(protein.atoms().size() == 327);
    assert(protein.bonds().size() > 300);
    assert(protein.radius() > 10.0F);
    assert(protein.cAlphaTrace().size() == 45);
    assert(protein.backboneTrace().size() >= 130);
    assert(protein.smoothCAlphaTrace().size() == 270);
    const auto ribbonFrames = protein.ribbonFrames();
    assert(ribbonFrames.size() == 270);
    for (const auto& frame : ribbonFrames) {
        const float sideLength = std::sqrt(frame.side.x * frame.side.x + frame.side.y * frame.side.y + frame.side.z * frame.side.z);
        const float normalLength = std::sqrt(frame.normal.x * frame.normal.x + frame.normal.y * frame.normal.y + frame.normal.z * frame.normal.z);
        const float orthogonality = frame.side.x * frame.normal.x + frame.side.y * frame.normal.y + frame.side.z * frame.normal.z;
        assert(std::abs(sideLength - 1.0F) < 1.0e-4F);
        assert(std::abs(normalLength - 1.0F) < 1.0e-4F);
        assert(std::abs(orthogonality) < 1.0e-4F);
    }
    std::size_t helixCAlpha = 0;
    std::size_t sheetCAlpha = 0;
    for (const auto& atom : protein.atoms()) {
        if (atom.name == "CA" && atom.secondaryStructure == jlmol::SecondaryStructure::Helix) ++helixCAlpha;
        if (atom.name == "CA" && atom.secondaryStructure == jlmol::SecondaryStructure::Sheet) ++sheetCAlpha;
    }
    assert(helixCAlpha == 21);
    assert(sheetCAlpha == 8);

    const auto water = std::make_shared<jlmol::Molecule>(jlmol::Molecule::load(std::filesystem::path(argv[3])));
    assert(water->atoms().size() == 3);
    assert(water->bonds().size() == 2);
    const auto waterId = scene.addMolecule(water);
    double opacity = 0.0;
    assert(jlmol_object_opacity(firstObject, &opacity) == 0 && opacity == 1.0);
    const auto generation = scene.generation();
    assert(jlmol_set_object_opacity(firstObject, 0.35) == 0);
    assert(scene.generation() > generation);
    assert(scene.objectOpacity(firstObject) == 0.35);
    assert(scene.objectOpacity(waterId) == 1.0);
    assert(jlmol_set_object_opacity(firstObject, std::numeric_limits<double>::quiet_NaN()) != 0);
    assert(jlmol_set_object_opacity(firstObject, -0.1) != 0);
    assert(jlmol_set_object_opacity(firstObject, 1.1) != 0);
    assert(jlmol_object_opacity(firstObject, &opacity) == 0 && opacity == 0.35);
    assert(jlmol_set_object_opacity(firstObject, 0.0) == 0);
    assert(jlmol_object_visible(firstObject) == 1);
    assert(scene.selectedObjectId() == firstObject);
    assert(scene.objectCount() == 2);
    assert(scene.activeObjectId() == waterId);
    assert(scene.setObjectVisible(firstObject, false));
    assert(!scene.objectVisible(firstObject));
    assert(scene.setRepresentation(firstObject, jlmol::Representation::Lines));
    assert(scene.representation(firstObject) == jlmol::Representation::Lines);
    assert(scene.representation(waterId) == jlmol::Representation::SpaceFilling);
    scene.selectAtom(waterId, water->atoms().front().id);
    assert(scene.selectedObjectId() == waterId);
    assert(scene.selectedAtom()->element == "O");
    scene.extendAtomSelection(waterId, water->atoms()[1].id);
    assert(scene.selectedAtoms().size() == 2);
    assert(scene.selectedAtomId() == water->atoms()[1].id);
    scene.extendAtomSelection(waterId, water->atoms()[2].id);
    assert(scene.selectedAtoms().size() == 3);
    assert(std::abs(jlmol::analysis::radiusOfGyration(*water) - 0.3135274256) < 1.0e-8);
    assert(scene.closeObject(waterId));
    assert(scene.objectCount() == 1);
    assert(scene.activeObjectId() == firstObject);
    scene.selectAtoms(firstObject, {1, 2, 3, 4});
    assert(scene.selectedAtoms().size() == 4);
    scene.extendAtomSelection(firstObject, 5);
    const auto shiftedSelection = scene.selectedAtomIds();
    assert(shiftedSelection.size() == 4);
    assert(shiftedSelection.front() == 2);
    assert(shiftedSelection.back() == 5);

    const auto cif = jlmol::Molecule::load(std::filesystem::path(argv[4]));
    assert(cif.atoms().size() == 4);
    assert(!cif.bonds().empty());
    const auto annotated = jlmol::Molecule::load(argv[5]);
    assert(annotated.atoms().size() == 6);
    using Structure = jlmol::SecondaryStructure;
    const std::array expected{Structure::Helix, Structure::Helix, Structure::Sheet,
                              Structure::Sheet, Structure::Coil, Structure::Coil};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        assert(annotated.atoms()[i].secondaryStructure == expected[i]);
    }
    const auto annotatedRibbon = annotated.ribbonFrames();
    assert(annotatedRibbon.size() == 24);
    assert(annotatedRibbon.front().secondaryStructure == Structure::Helix);
    assert(annotatedRibbon.back().secondaryStructure == Structure::Sheet);
    jlmol_set_active_scene(nullptr);

    std::cout << "native core tests passed\n";
    return 0;
}

