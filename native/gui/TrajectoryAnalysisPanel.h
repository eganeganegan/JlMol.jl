#pragma once

#include "core/MolecularScene.h"

#include <QWidget>
#include <array>

class QCheckBox;
class QComboBox;
class QLabel;
class QPainter;
class QPushButton;
class QSpinBox;
class QTimer;

namespace jlmol {

class CommandDispatcher;
class JuliaRuntime;

class TrajectoryPlot final : public QWidget {
    Q_OBJECT
public:
    explicit TrajectoryPlot(QWidget* parent);
    void clear();
    void setSeries(std::vector<double> values, std::vector<std::uint64_t> ids, std::size_t step,
                   std::size_t domain, bool frames);
    void setScatter(std::vector<double> x, std::vector<double> y, std::vector<std::uint64_t> frames,
                    std::array<double, 2> explained, std::vector<int> groups);
    void setMarkers(std::vector<std::size_t> markers);
    [[nodiscard]] const std::vector<double>& values() const { return values_; }
    [[nodiscard]] const std::vector<std::size_t>& markers() const { return markers_; }
    [[nodiscard]] const std::vector<int>& groups() const { return groups_; }
    [[nodiscard]] QPointF pointPosition(std::size_t index) const;

signals:
    void pointActivated(std::size_t index);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool event(QEvent* event) override;

private:
    [[nodiscard]] QRectF chartArea() const;
    [[nodiscard]] double xPosition(std::size_t ordinal) const;
    [[nodiscard]] std::optional<std::size_t> nearestPoint(QPointF point) const;
    void paintScatter(QPainter& painter, const QRectF& area) const;

    std::vector<double> values_;
    std::vector<double> scatterX_;
    std::vector<int> groups_;
    std::array<double, 2> explained_{};
    QRectF scatterBounds_;
    std::vector<std::uint64_t> ids_;
    std::vector<std::size_t> markers_;
    std::size_t step_{1};
    std::size_t domain_{1};
    bool frames_{true};
    double maximum_{1.0};
};

class TrajectoryAnalysisPanel final : public QWidget {
public:
    TrajectoryAnalysisPanel(std::shared_ptr<MolecularScene> scene, CommandDispatcher* dispatcher,
                            JuliaRuntime* runtime, QWidget* parent);
    void refresh();

protected:
    void showEvent(QShowEvent* event) override;

private:
    enum class Metric { Rmsd, Rmsf, Pca };
    [[nodiscard]] Metric metric() const;
    void clearResult();
    void calculate();
    void activatePoint(std::size_t index);
    void updateMarkers(const MolecularObject& object);

    std::shared_ptr<MolecularScene> scene_;
    CommandDispatcher* dispatcher_;
    JuliaRuntime* runtime_;
    std::optional<ObjectId> object_;
    std::shared_ptr<const Trajectory> trajectory_;
    std::vector<std::uint64_t> selection_;
    std::vector<std::uint64_t> pointIds_;
    bool requested_{};
    QComboBox* metric_{};
    QCheckBox* align_{};
    QCheckBox* selectedOnly_{};
    QSpinBox* reference_{};
    QSpinBox* stride_{};
    QSpinBox* clusters_{};
    QPushButton* calculate_{};
    QLabel* status_{};
    TrajectoryPlot* plot_{};
    QTimer* selectionTimer_{};
};

} // namespace jlmol
