#pragma once

#include <QString>
#include <mutex>

namespace jlmol {

class JuliaRuntime final {
public:
    JuliaRuntime() = default;
    ~JuliaRuntime();

    JuliaRuntime(const JuliaRuntime&) = delete;
    JuliaRuntime& operator=(const JuliaRuntime&) = delete;

    bool initialize(QString* error = nullptr);
    [[nodiscard]] bool isAvailable() const noexcept { return available_; }
    [[nodiscard]] QString version() const { return version_; }
    QString evaluate(const QString& source);
    void shutdown();

private:
    bool available_{};
    bool ownsRuntime_{};
    QString version_;
    std::mutex evaluationMutex_;
};

} // namespace jlmol

