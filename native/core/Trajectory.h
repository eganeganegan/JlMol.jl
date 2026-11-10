#pragma once

#include "core/Molecule.h"

#include <memory>
#include <optional>

namespace jlmol {

struct FrameMetadata {
    // Cell basis vectors in consecutive triples, in angstroms. Zero means no cell.
    std::array<double, 9> cell{};
    std::optional<double> timePs;
};

struct TrajectoryFrame {
    std::vector<Vec3> positions;
    FrameMetadata metadata;
};

class Trajectory {
public:
    Trajectory(const std::filesystem::path& path, std::size_t atomCount);
    ~Trajectory();
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] std::size_t frameCount() const noexcept { return frameCount_; }
    [[nodiscard]] std::shared_ptr<const TrajectoryFrame> readFrame(std::size_t index) const;

private:
    struct Reader;
    std::unique_ptr<Reader> reader_;
    std::filesystem::path path_;
    std::size_t atomCount_{};
    std::size_t frameCount_{};
};

} // namespace jlmol
