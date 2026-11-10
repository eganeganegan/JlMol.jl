#include "core/Trajectory.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <list>
#include <mutex>
#include <stdexcept>

#ifdef JLMOL_HAS_CHEMFILES
#include <chemfiles.hpp>
#endif

namespace jlmol {

struct Trajectory::Reader {
#ifdef JLMOL_HAS_CHEMFILES
    explicit Reader(const std::filesystem::path& path) : file(path.string()) {
        auto extension = path.extension().string();
        for (auto& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        // DCD headers do not reliably identify a physical time unit.
        timeInPicoseconds = extension == ".xtc" || extension == ".trr";
    }
    chemfiles::Trajectory file;
    bool timeInPicoseconds{};
    std::mutex mutex;
    std::list<std::pair<std::size_t, std::shared_ptr<const TrajectoryFrame>>> cache;
    std::size_t cachedBytes{};
#endif
};

Trajectory::Trajectory(const std::filesystem::path& path, std::size_t atomCount)
    : path_(std::filesystem::absolute(path)), atomCount_(atomCount) {
#ifdef JLMOL_HAS_CHEMFILES
    if (!atomCount_) throw std::invalid_argument("Load a topology with atoms before attaching a trajectory");
    reader_ = std::make_unique<Reader>(path_);
    frameCount_ = reader_->file.nsteps();
    if (frameCount_ == 0) throw std::runtime_error("The trajectory has no frames");
#else
    throw std::runtime_error("Trajectory support requires a build with Chemfiles");
#endif
}

Trajectory::~Trajectory() = default;

std::shared_ptr<const TrajectoryFrame> Trajectory::readFrame(std::size_t index) const {
#ifdef JLMOL_HAS_CHEMFILES
    if (index >= frameCount_) throw std::out_of_range("Trajectory frame is out of range");
    auto& reader = *reader_;
    std::scoped_lock lock(reader.mutex);
    const auto cached = std::find_if(reader.cache.begin(), reader.cache.end(),
        [index](const auto& entry) { return entry.first == index; });
    if (cached != reader.cache.end()) {
        reader.cache.splice(reader.cache.begin(), reader.cache, cached);
        return reader.cache.front().second;
    }
    auto source = reader.file.read_step(index);
    if (source.size() != atomCount_) {
        throw std::runtime_error("Trajectory frame " + std::to_string(index + 1) + " has " +
            std::to_string(source.size()) + " atoms; the topology has " + std::to_string(atomCount_));
    }
    // TRR can contain velocity or force records with no position block.
    if (!source.get<chemfiles::Property::BOOL>("has_positions").value_or(true)) {
        throw std::runtime_error("Trajectory frame " + std::to_string(index + 1) + " has no atom positions");
    }
    auto frame = std::make_shared<TrajectoryFrame>();
    frame->positions.reserve(atomCount_);
    const auto coordinate = [](double value) {
        if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
            throw std::runtime_error("Trajectory frame contains a non-finite or out-of-range coordinate");
        }
        return static_cast<float>(value);
    };
    for (const auto& p : source.positions()) {
        frame->positions.push_back({coordinate(p[0]), coordinate(p[1]), coordinate(p[2])});
    }
    const auto matrix = source.cell().matrix();
    for (std::size_t column = 0; column < 3; ++column) {
        for (std::size_t row = 0; row < 3; ++row) {
            if (!std::isfinite(matrix[row][column])) throw std::runtime_error("Trajectory frame has an invalid unit cell");
            frame->metadata.cell[column * 3 + row] = matrix[row][column];
        }
    }
    if (reader.timeInPicoseconds) {
        const auto time = source.get<chemfiles::Property::DOUBLE>("time");
        if (time && std::isfinite(*time)) frame->metadata.timePs = *time;
    }
    constexpr std::size_t byteLimit = 32 * 1024 * 1024;
    constexpr std::size_t entryLimit = 8;
    const auto bytes = frame->positions.capacity() * sizeof(Vec3);
    if (bytes <= byteLimit) {
        while (reader.cache.size() >= entryLimit || reader.cachedBytes > byteLimit - bytes) {
            reader.cachedBytes -= reader.cache.back().second->positions.capacity() * sizeof(Vec3);
            reader.cache.pop_back();
        }
        reader.cache.emplace_front(index, frame);
        reader.cachedBytes += bytes;
    }
    return frame;
#else
    (void)index;
    throw std::runtime_error("Trajectory support requires a build with Chemfiles");
#endif
}

} // namespace jlmol
