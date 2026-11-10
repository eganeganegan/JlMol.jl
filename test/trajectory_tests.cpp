#include "bridge/JlMolAPI.h"
#include "core/MolecularScene.h"

#include <chemfiles.hpp>

#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::size_t frameCount = 12;
constexpr std::size_t atomCount = 12; // Exercise the compressed coordinate path in XTC.
const std::array<const char*, 5> filenames{"test.xtc", "test.trr", "test.dcd", "variable.xyz", "topology.xyz"};

void checkApi(int status) {
    if (status != 0) throw std::runtime_error(jlmol_get_last_error());
}

struct TemporaryFiles {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("jlmol-trajectory-test-" + std::to_string(std::random_device{}()));
    TemporaryFiles() { check(std::filesystem::create_directory(dir), "Could not create trajectory test directory"); }
    ~TemporaryFiles() {
        std::error_code ignored;
        for (const auto* file : filenames) std::filesystem::remove(dir / file, ignored);
        std::filesystem::remove(dir, ignored);
    }
};

chemfiles::Frame makeFrame(std::size_t step, bool full = true) {
    chemfiles::Frame frame;
    const auto shift = static_cast<double>(step) * 0.5;
    for (std::size_t group = 0; group < atomCount / 3; ++group) {
        const double y = group * 3.0;
        frame.add_atom(chemfiles::Atom("O"), {shift, y, 0.0});
        frame.add_atom(chemfiles::Atom("H"), {shift + 0.9572, y, 0.0});
        if (full || group != 0) frame.add_atom(chemfiles::Atom("H"), {shift - 0.239987, y + 0.927297, 0.0});
    }
    frame.set_cell(chemfiles::UnitCell({20.0 + shift, 21.0, 22.0}, {90.0, 100.0, 90.0}));
    frame.set_step(step * 10);
    frame.set("time", static_cast<double>(step) * 2.0);
    return frame;
}

} // namespace

int main() {
    try {
        TemporaryFiles files;
        const auto topology = files.dir / filenames[4];
        {
            chemfiles::Trajectory writer(topology.string(), 'w');
            writer.write(makeFrame(0));
        }
        auto molecule = std::make_shared<jlmol::Molecule>(jlmol::Molecule::load(topology));
        jlmol::MolecularScene scene;
        const auto id = scene.addMolecule(molecule);
        jlmol_set_active_scene(&scene);
        check(scene.setObjectOpacity(id, 0.45), "Could not set opacity");
        check(scene.setRepresentation(id, jlmol::Representation::SpaceFilling), "Could not set representation");
        scene.selectAtom(id, molecule->atoms().front().id);

        std::size_t count{}, index{};
        double time{};
        double cell[9]{};
        check(jlmol_trajectory_state(id, &count, &index, &time, cell) == 0 && count == 0,
              "A static structure should report no trajectory");
        for (std::size_t format = 0; format < 3; ++format) {
            const auto path = files.dir / filenames[format];
            {
                chemfiles::Trajectory writer(path.string(), 'w');
                for (std::size_t frame = 0; frame < frameCount; ++frame) writer.write(makeFrame(frame));
            }
            checkApi(jlmol_attach_trajectory(id, path.string().c_str()));
            const auto trajectory = scene.object(id)->trajectory;
            check(trajectory->frameCount() == frameCount, "Wrong frame count");
            const auto first = trajectory->readFrame(0);
            check(first == trajectory->readFrame(0), "The last frame should be cached");
            for (std::size_t frame = 1; frame < frameCount; ++frame) (void)trajectory->readFrame(frame);
            check(first != trajectory->readFrame(0), "The frame cache should evict older entries");

            double coordinates[atomCount * 3]{};
            check(jlmol_copy_trajectory_frame(id, 11, coordinates, atomCount) == 0 && std::abs(coordinates[0] - 5.5) < 0.02,
                  "Random access should return the expected coordinates in angstroms");
            check(jlmol_copy_trajectory_frame(id, 11, coordinates, 2) != 0, "Accepted a too-small frame buffer");
            check(scene.object(id)->frameIndex == 0, "A copied frame should not move the viewport");
            checkApi(jlmol_set_trajectory_frame(id, 4));
            check(jlmol_trajectory_state(id, &count, &index, &time, cell) == 0 && count == frameCount && index == 4,
                  "Wrong displayed frame state");
            check(std::abs(cell[0] - 22.0) < 0.02 && std::abs(cell[4] - 21.0) < 0.02 &&
                  cell[6] < -3.7 && cell[8] > 21.5, "Wrong triclinic cell basis");
            check(format == 2 ? std::isnan(time) : std::abs(time - 8.0) < 0.001, "Unexpected trajectory time");
            const auto current = scene.object(id);
            check(std::abs(current->molecule->atoms()[0].position.x - 2.0) < 0.02, "Display did not seek");
            check(current->opacity == 0.45 && current->representation == jlmol::Representation::SpaceFilling &&
                  current->molecule->bonds().size() == molecule->bonds().size(), "Frame seek changed structure styling or bonds");
            check(scene.selectedAtomId() == molecule->atoms().front().id &&
                  std::abs(scene.selectedAtom()->position.x - 2.0) < 0.02, "Selection did not follow its atom");
            check(jlmol_set_trajectory_frame(id, frameCount) != 0 && scene.object(id)->molecule == current->molecule,
                  "An invalid seek changed the active frame");
            check(molecule->atoms()[0].position.x == 0.0F, "Frame seek mutated an earlier structure snapshot");
            std::cout << filenames[format] << ": random access, metadata, cache, and scene state passed\n";
        }

        const auto variable = files.dir / filenames[3];
        {
            chemfiles::Trajectory writer(variable.string(), 'w');
            writer.write(makeFrame(0));
            writer.write(makeFrame(1, false));
        }
        checkApi(jlmol_attach_trajectory(id, variable.string().c_str()));
        const auto original = scene.object(id)->molecule;
        const auto oldTrajectory = scene.object(id)->trajectory;
        check(jlmol_attach_trajectory(id, (files.dir / "missing.xtc").string().c_str()) != 0 &&
              scene.object(id)->trajectory == oldTrajectory && scene.object(id)->molecule == original,
              "A failed replacement changed the current trajectory");
        check(jlmol_set_trajectory_frame(id, 1) != 0 && scene.object(id)->molecule == original,
              "A frame with mismatched atom count changed the scene");
        const auto mismatchedId = scene.addMolecule(std::make_shared<jlmol::Molecule>());
        check(jlmol_attach_trajectory(mismatchedId, variable.string().c_str()) != 0 &&
              !scene.object(mismatchedId)->trajectory, "An invalid topology accepted a trajectory");
        jlmol_set_active_scene(nullptr);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
