#include "gui/TrajectoryAnalysisPanel.h"

#include "commands/CommandDispatcher.h"
#include "scripting/JuliaRuntime.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHelpEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_set>

namespace jlmol {
namespace {

constexpr std::size_t maxSamples = 5000;
constexpr std::size_t maxAtomFrames = 20'000'000;
constexpr std::size_t maxAtomPoints = 20'000;
constexpr std::size_t maxPcaSamples = 500;
constexpr std::size_t maxPcaAtomFrames = 2'000'000;
constexpr int maxClusters = 8;

QString number(double value) { return QString::number(value, 'g', 4); }

} // namespace

TrajectoryPlot::TrajectoryPlot(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("TrajectoryPlot"));
    setMinimumHeight(186);
    setAccessibleName(QStringLiteral("Trajectory analysis plot"));
}

void TrajectoryPlot::clear() {
    values_.clear();
    scatterX_.clear();
    groups_.clear();
    ids_.clear();
    markers_.clear();
    update();
}

void TrajectoryPlot::setSeries(std::vector<double> values, std::vector<std::uint64_t> ids,
                               std::size_t step, std::size_t domain, bool frames) {
    scatterX_.clear();
    groups_.clear();
    values_ = std::move(values);
    ids_ = std::move(ids);
    step_ = step;
    domain_ = domain;
    frames_ = frames;
    maximum_ = *std::max_element(values_.begin(), values_.end());
    maximum_ = maximum_ == 0.0 ? 1.0 : maximum_ * 1.05;
    markers_.clear();
    update();
}

void TrajectoryPlot::setScatter(std::vector<double> x, std::vector<double> y,
                                std::vector<std::uint64_t> frames, std::array<double, 2> explained,
                                std::vector<int> groups) {
    scatterX_ = std::move(x);
    values_ = std::move(y);
    ids_ = std::move(frames);
    groups_ = std::move(groups);
    explained_ = explained;
    frames_ = true;
    markers_.clear();
    const auto [xmin, xmax] = std::minmax_element(scatterX_.begin(), scatterX_.end());
    const auto [ymin, ymax] = std::minmax_element(values_.begin(), values_.end());
    const auto span = std::max({*xmax - *xmin, *ymax - *ymin, 1e-12});
    const auto xpad = std::max((*xmax - *xmin) * 0.08, span * 0.04);
    const auto ypad = std::max((*ymax - *ymin) * 0.08, span * 0.04);
    scatterBounds_ = QRectF(*xmin - xpad, *ymin - ypad,
                            *xmax - *xmin + 2 * xpad, *ymax - *ymin + 2 * ypad);
    update();
}

void TrajectoryPlot::setMarkers(std::vector<std::size_t> markers) {
    if (markers_ == markers) return;
    markers_ = std::move(markers);
    update();
}

QRectF TrajectoryPlot::chartArea() const { return QRectF(rect()).adjusted(63, 27, -22, -41); }

double TrajectoryPlot::xPosition(std::size_t ordinal) const {
    const auto area = chartArea();
    return domain_ <= 1 ? area.center().x() :
        area.left() + area.width() * static_cast<double>(ordinal - 1) / static_cast<double>(domain_ - 1);
}

QPointF TrajectoryPlot::pointPosition(std::size_t index) const {
    const auto area = chartArea();
    if (!scatterX_.empty()) {
        const auto x = (scatterX_.at(index) - scatterBounds_.left()) / scatterBounds_.width();
        const auto y = (values_.at(index) - scatterBounds_.top()) / scatterBounds_.height();
        return {area.left() + area.width() * x, area.bottom() - area.height() * y};
    }
    return {xPosition(1 + index * step_), area.bottom() - area.height() * values_.at(index) / maximum_};
}

std::optional<std::size_t> TrajectoryPlot::nearestPoint(QPointF point) const {
    const auto area = chartArea();
    if (values_.empty() || !area.isValid() || !area.adjusted(-4, -4, 4, 4).contains(point)) return std::nullopt;
    if (!scatterX_.empty()) {
        std::optional<std::size_t> best;
        double nearest = 12.0 * 12.0;
        for (std::size_t i = 0; i < values_.size(); ++i) {
            const auto delta = pointPosition(i) - point;
            const auto distance = delta.x() * delta.x() + delta.y() * delta.y();
            if (distance < nearest) {
                best = i;
                nearest = distance;
            }
        }
        return best;
    }
    const auto fraction = std::clamp((point.x() - area.left()) / area.width(), 0.0, 1.0);
    const auto index = static_cast<std::size_t>(std::llround(fraction * (domain_ - 1) / step_));
    return std::min(index, values_.size() - 1);
}

void TrajectoryPlot::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().base());
    painter.setPen(palette().text().color());
    if (values_.empty()) {
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No trajectory analysis to display"));
        return;
    }
    const auto area = chartArea();
    if (!area.isValid()) return;
    if (!scatterX_.empty()) {
        paintScatter(painter, area);
        return;
    }
    const auto textColor = palette().text().color();
    const auto seriesColor = palette().highlight().color();
    for (int i = 0; i <= 4; ++i) {
        const double y = area.bottom() - area.height() * i / 4.0;
        painter.setPen(palette().midlight().color());
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        painter.setPen(textColor);
        painter.drawText(QRectF(1, y - 9, area.left() - 6, 18),
                         Qt::AlignRight | Qt::AlignVCenter, number(maximum_ * i / 4.0));
    }
    painter.drawText(QRectF(area.left(), 0, area.width(), 24), Qt::AlignVCenter,
                     frames_ ? QStringLiteral("RMSD (Å)") : QStringLiteral("RMSF (Å)"));
    painter.drawText(QRectF(area.left() - 8, area.bottom() + 3, area.width() + 16, 17),
                     Qt::AlignLeft, QStringLiteral("1"));
    if (domain_ > 1) painter.drawText(QRectF(area.left(), area.bottom() + 3, area.width() + 16, 17),
                                    Qt::AlignRight, QString::number(domain_));
    painter.drawText(QRectF(area.left(), area.bottom() + 20, area.width(), 18), Qt::AlignCenter,
                     frames_ ? QStringLiteral("Frame") : QStringLiteral("Atom order"));
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path(pointPosition(0));
    for (std::size_t i = 1; i < values_.size(); ++i) path.lineTo(pointPosition(i));
    painter.setPen(QPen(seriesColor, 1.6));
    painter.drawPath(path);
    if (values_.size() <= 60) {
        painter.setBrush(seriesColor);
        for (std::size_t i = 0; i < values_.size(); ++i) painter.drawEllipse(pointPosition(i), 2.4, 2.4);
    }
    painter.setBrush(palette().base());
    painter.setPen(QPen(textColor, 1.2, Qt::DashLine));
    for (const auto marker : markers_) {
        if (frames_) painter.drawLine(QPointF(xPosition(marker), area.top()), QPointF(xPosition(marker), area.bottom()));
        if ((marker - 1) % step_ == 0) {
            const auto point = (marker - 1) / step_;
            if (point < values_.size()) painter.drawEllipse(pointPosition(point), 4, 4);
        }
    }
}

void TrajectoryPlot::paintScatter(QPainter& painter, const QRectF& area) const {
    const auto textColor = palette().text().color();
    for (int i = 0; i <= 4; ++i) {
        const double fraction = i / 4.0;
        const double x = area.left() + area.width() * fraction;
        const double y = area.bottom() - area.height() * fraction;
        painter.setPen(palette().midlight().color());
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        painter.setPen(textColor);
        painter.drawText(QRectF(1, y - 9, area.left() - 6, 18), Qt::AlignRight | Qt::AlignVCenter,
                         number(scatterBounds_.top() + fraction * scatterBounds_.height()));
        if (i % 2 == 0) {
            const auto label = number(scatterBounds_.left() + fraction * scatterBounds_.width());
            painter.drawText(QPointF(x - painter.fontMetrics().horizontalAdvance(label) / 2.0, area.bottom() + 15), label);
        }
    }
    painter.setPen(textColor);
    painter.drawText(QRectF(area.left(), 0, area.width(), 24), Qt::AlignVCenter,
                     QStringLiteral("PC2 (Å, %1%)").arg(100 * explained_[1], 0, 'f', 1));
    painter.drawText(QRectF(area.left(), area.bottom() + 20, area.width(), 18), Qt::AlignCenter,
                     QStringLiteral("PC1 (Å, %1%)").arg(100 * explained_[0], 0, 'f', 1));
    if (markers_.empty()) {
        painter.drawText(QRectF(area.left(), 0, area.width(), 24), Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("Current frame not sampled"));
    }
    static const std::array<QColor, maxClusters> colors{
        QColor(46, 125, 200), QColor(218, 107, 28), QColor(30, 155, 104), QColor(167, 89, 194),
        QColor(204, 56, 96), QColor(165, 130, 38), QColor(15, 146, 162), QColor(120, 123, 136)};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    for (std::size_t i = 0; i < values_.size(); ++i) {
        painter.setBrush(groups_.empty() ? palette().highlight().color() : colors[groups_[i] - 1]);
        painter.drawEllipse(pointPosition(i), 3.1, 3.1);
    }
    painter.setPen(QPen(textColor, 1.7));
    painter.setBrush(Qt::NoBrush);
    for (const auto marker : markers_) {
        if (marker > 0 && marker <= values_.size()) painter.drawEllipse(pointPosition(marker - 1), 6.4, 6.4);
    }
}

void TrajectoryPlot::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (const auto index = nearestPoint(event->position())) {
            emit pointActivated(*index);
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

bool TrajectoryPlot::event(QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        const auto* help = static_cast<QHelpEvent*>(event);
        if (const auto index = nearestPoint(help->pos())) {
            const auto label = frames_ ? QStringLiteral("Frame %1") : QStringLiteral("Atom ID %1");
            auto detail = label.arg(ids_[*index]);
            if (scatterX_.empty()) {
                detail += QStringLiteral(": %1 Å").arg(number(values_[*index]));
            } else {
                detail += QStringLiteral(": PC1 %1 Å, PC2 %2 Å").arg(number(scatterX_[*index]), number(values_[*index]));
                if (!groups_.empty()) detail += QStringLiteral(" : cluster %1").arg(groups_[*index]);
            }
            QToolTip::showText(help->globalPos(), detail, this);
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QWidget::event(event);
}

TrajectoryAnalysisPanel::TrajectoryAnalysisPanel(std::shared_ptr<MolecularScene> scene, CommandDispatcher* dispatcher,
                                                 JuliaRuntime* runtime, QWidget* parent)
    : QWidget(parent), scene_(std::move(scene)), dispatcher_(dispatcher), runtime_(runtime) {
    auto* layout = new QVBoxLayout(this);
    auto* controls = new QHBoxLayout;
    layout->addLayout(controls);
    metric_ = new QComboBox(this);
    metric_->setObjectName(QStringLiteral("TrajectoryMetric"));
    metric_->addItems({QStringLiteral("RMSD"), QStringLiteral("RMSF"), QStringLiteral("PCA")});
    metric_->setAccessibleName(QStringLiteral("Trajectory metric"));
    controls->addWidget(metric_);
    align_ = new QCheckBox(QStringLiteral("Align"), this);
    align_->setObjectName(QStringLiteral("TrajectoryAlign"));
    align_->setChecked(true);
    controls->addWidget(align_);
    selectedOnly_ = new QCheckBox(QStringLiteral("Selected atoms"), this);
    selectedOnly_->setObjectName(QStringLiteral("TrajectorySelectedOnly"));
    controls->addWidget(selectedOnly_);
    reference_ = new QSpinBox(this);
    reference_->setRange(1, 1);
    reference_->setPrefix(QStringLiteral("Ref: "));
    reference_->setAccessibleName(QStringLiteral("Reference frame"));
    reference_->setKeyboardTracking(false);
    controls->addWidget(reference_);
    stride_ = new QSpinBox(this);
    stride_->setRange(1, 1);
    stride_->setObjectName(QStringLiteral("TrajectoryStride"));
    stride_->setPrefix(QStringLiteral("Stride: "));
    stride_->setAccessibleName(QStringLiteral("Frame sampling stride"));
    stride_->setToolTip(QStringLiteral("Stride 1 reads every frame; stride 10 reads frames 1, 11, 21, and so on."));
    stride_->setKeyboardTracking(false);
    controls->addWidget(stride_);
    clusters_ = new QSpinBox(this);
    clusters_->setObjectName(QStringLiteral("TrajectoryClusters"));
    clusters_->setAccessibleName(QStringLiteral("PCA cluster count"));
    clusters_->setRange(0, maxClusters);
    clusters_->setPrefix(QStringLiteral("Clusters: "));
    clusters_->setSpecialValueText(QStringLiteral("Clusters off"));
    clusters_->setToolTip(QStringLiteral("Group points in the displayed PC1 and PC2 projection."));
    clusters_->setKeyboardTracking(false);
    clusters_->hide();
    controls->addWidget(clusters_);
    calculate_ = new QPushButton(QStringLiteral("Calculate"), this);
    calculate_->setObjectName(QStringLiteral("TrajectoryCalculate"));
    controls->addWidget(calculate_);
    controls->addStretch();
    status_ = new QLabel(this);
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    plot_ = new TrajectoryPlot(this);
    layout->addWidget(plot_, 1);
    selectionTimer_ = new QTimer(this);
    selectionTimer_->setSingleShot(true);
    selectionTimer_->setInterval(180);
    connect(selectionTimer_, &QTimer::timeout, this, [this] {
        if (requested_ && isVisible()) calculate();
    });
    const auto invalidate = [this] {
        clearResult();
        refresh();
    };
    connect(metric_, &QComboBox::currentIndexChanged, this, invalidate);
    connect(align_, &QCheckBox::toggled, this, invalidate);
    connect(selectedOnly_, &QCheckBox::toggled, this, invalidate);
    connect(reference_, &QSpinBox::valueChanged, this, invalidate);
    connect(stride_, &QSpinBox::valueChanged, this, invalidate);
    connect(clusters_, &QSpinBox::valueChanged, this, invalidate);
    connect(calculate_, &QPushButton::clicked, this, &TrajectoryAnalysisPanel::calculate);
    connect(plot_, &TrajectoryPlot::pointActivated, this, &TrajectoryAnalysisPanel::activatePoint);
}

TrajectoryAnalysisPanel::Metric TrajectoryAnalysisPanel::metric() const {
    return static_cast<Metric>(metric_->currentIndex());
}

TrajectoryAnalysisPanel::Settings TrajectoryAnalysisPanel::settings() const {
    return {metric(), align_->isChecked(), selectedOnly_->isChecked(), reference_->value(),
            stride_->value(), clusters_->value(), !pointIds_.empty()};
}

bool TrajectoryAnalysisPanel::restoreSettings(const Settings& state) {
    clearResult();
    refresh();
    {
        const QSignalBlocker metricBlock(metric_), alignBlock(align_), selectionBlock(selectedOnly_),
            referenceBlock(reference_), strideBlock(stride_), clusterBlock(clusters_);
        metric_->setCurrentIndex(static_cast<int>(state.metric));
        align_->setChecked(state.align);
        selectedOnly_->setChecked(state.selectedOnly);
        reference_->setValue(state.reference);
        stride_->setValue(state.stride);
        clusters_->setValue(state.clusters);
    }
    refresh();
    if (state.calculated) calculate();
    return !state.calculated || !pointIds_.empty();
}

void TrajectoryAnalysisPanel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    refresh();
    if (requested_ && selectedOnly_->isChecked() && !selection_.empty() && plot_->values().empty()) {
        selectionTimer_->start();
    }
}

void TrajectoryAnalysisPanel::clearResult() {
    requested_ = false;
    selectionTimer_->stop();
    pointIds_.clear();
    plot_->clear();
}

void TrajectoryAnalysisPanel::refresh() {
    const auto active = scene_->activeObjectId();
    const auto object = active ? scene_->object(*active) : std::nullopt;
    const auto trajectory = object ? object->trajectory : nullptr;
    if (object_ != active || trajectory_ != trajectory) {
        clearResult();
        object_ = active;
        trajectory_ = trajectory;
        const auto count = trajectory ? trajectory->frameCount() : 1;
        const int maximum = static_cast<int>(std::min(count, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        const auto atomCount = object ? object->molecule->atoms().size() : 1;
        const auto target = std::clamp(maxAtomFrames / std::max<std::size_t>(atomCount, 1),
                                      std::size_t{1}, std::size_t{1000});
        const QSignalBlocker referenceBlock(reference_);
        const QSignalBlocker strideBlock(stride_);
        reference_->setRange(1, maximum);
        reference_->setValue(1);
        stride_->setRange(1, maximum);
        stride_->setValue(static_cast<int>(std::min<std::size_t>((count - 1) / target + 1, maximum)));
    }

    const auto selection = active && scene_->selectedObjectId() == active
        ? scene_->selectedAtomIds() : std::vector<std::uint64_t>{};
    const bool selectionChanged = selection_ != selection;
    if (selectionChanged && requested_ && selectedOnly_->isChecked()) {
        pointIds_.clear();
        plot_->clear();
        if (isVisible() && !selection.empty()) selectionTimer_->start();
    }
    selection_ = selection;
    const bool ready = trajectory && runtime_->isAvailable();
    const bool hasAtoms = !selectedOnly_->isChecked() || !selection_.empty();
    reference_->setEnabled(ready && (metric() == Metric::Rmsd || align_->isChecked()));
    stride_->setEnabled(ready);
    clusters_->setVisible(metric() == Metric::Pca);
    clusters_->setEnabled(ready);
    calculate_->setEnabled(ready && hasAtoms);
    if (!trajectory) status_->setText(QStringLiteral("Attach a trajectory to the active structure."));
    else if (!ready) status_->setText(QStringLiteral("Trajectory analysis requires the embedded Julia runtime."));
    else if (!hasAtoms) status_->setText(QStringLiteral("Select atoms in the active structure to analyze them."));
    else if (!requested_) status_->setText(QStringLiteral("Choose settings and calculate. Click a result to seek or select an atom."));
    else if (selectionTimer_->isActive()) status_->setText(QStringLiteral("Updating analysis for the atom selection…"));
    if (object && !pointIds_.empty() && (metric() != Metric::Rmsf || selectionChanged)) updateMarkers(*object);
}

void TrajectoryAnalysisPanel::calculate() {
    selectionTimer_->stop();
    refresh();
    if (!calculate_->isEnabled() || !object_ || !trajectory_) return;
    requested_ = true;
    pointIds_.clear();
    plot_->clear();
    const auto object = scene_->object(*object_);
    if (!object || object->trajectory != trajectory_) return;
    const auto atomCount = object->molecule->atoms().size();
    const auto frameCount = trajectory_->frameCount();
    const auto step = static_cast<std::size_t>(stride_->value());
    const auto samples = (frameCount - 1) / step + 1;
    const auto mode = metric();
    const bool pca = mode == Metric::Pca;
    const bool frames = mode != Metric::Rmsf;
    const bool subset = selectedOnly_->isChecked();
    const auto selectedCount = subset ? selection_.size() : atomCount;
    auto budget = std::min(maxSamples, maxAtomFrames / std::max<std::size_t>(1, atomCount));
    if (pca) budget = std::min({budget, maxPcaSamples, maxPcaAtomFrames / std::max<std::size_t>(1, selectedCount)});
    if (budget == 0 || (pca && budget < 2)) {
        status_->setText(QStringLiteral("Too many atoms for this calculation. Try a smaller selection or structure."));
        return;
    }
    if (samples > budget) {
        status_->setText(QStringLiteral("Too many sampled frames. Use a stride of at least %1.")
                             .arg((frameCount - 1) / budget + 1));
        return;
    }
    if (!frames && selectedCount > maxAtomPoints) {
        status_->setText(QStringLiteral("RMSF plots support up to 20,000 atoms. Analyze a selection."));
        return;
    }
    if (align_->isChecked() && selectedCount < 3) {
        status_->setText(QStringLiteral("Alignment needs three non-collinear atoms. Select more atoms or turn off Align."));
        return;
    }
    const int groupCount = pca ? clusters_->value() : 0;
    if (pca && samples < 2) {
        status_->setText(QStringLiteral("PCA needs at least two sampled frames. Reduce the stride or attach a longer trajectory."));
        return;
    }
    if (groupCount > static_cast<int>(samples)) {
        status_->setText(QStringLiteral("Choose no more clusters than sampled frames."));
        return;
    }

    QStringList ids;
    if (subset) for (auto id : selection_) ids.push_back(QString::number(id));
    const auto atomIds = subset ? QStringLiteral("UInt64[%1]").arg(ids.join(',')) : QStringLiteral("nothing");
    const auto call = QStringLiteral("JlMol.trajectory_%1(JlMol.ActiveStructure(UInt64(%2)); "
                                     "reference=%3, align=%4, stride=%5, atom_ids=%6)")
        .arg(pca ? QStringLiteral("pca") : frames ? QStringLiteral("rmsd") : QStringLiteral("rmsf")).arg(*object_)
        .arg(reference_->value()).arg(align_->isChecked() ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(step).arg(atomIds);
    auto source = QStringLiteral("using JlMol\n");
    if (pca) {
        source += QStringLiteral("let result = %1\n").arg(call);
        if (groupCount > 0) {
            source += QStringLiteral("  groups = JlMol.cluster_conformations(result.scores; k=%1)\n"
                                     "  vcat(vec(result.scores), result.explained_fraction, "
                                     "Float64.(groups.assignments), Float64[groups.converged])\n").arg(groupCount);
        } else {
            source += QStringLiteral("  vcat(vec(result.scores), result.explained_fraction)\n");
        }
        source += QStringLiteral("end");
    } else {
        source += call + (frames ? QString{} : QStringLiteral(".values"));
    }
    auto result = runtime_->evaluateSeries(source);
    const auto points = frames ? samples : selectedCount;
    const auto expected = pca ? 2 * samples + 2 + (groupCount > 0 ? samples + 1 : 0) : points;
    if (!result.error.isEmpty() || result.values.size() != expected ||
        std::any_of(result.values.begin(), result.values.end(), [pca](double v) { return !std::isfinite(v) || (!pca && v < 0); })) {
        status_->setText(result.error.isEmpty() ? QStringLiteral("Julia returned an invalid analysis series.") : result.error);
        return;
    }
    auto summary = QStringLiteral("%1 : %2 frames : %3 atoms")
        .arg(QString::fromStdString(object->molecule->name())).arg(samples).arg(selectedCount);
    pointIds_.reserve(points);
    if (frames) {
        for (std::size_t i = 0; i < samples; ++i) pointIds_.push_back(1 + i * step);
    } else if (subset) {
        pointIds_ = selection_;
    } else {
        for (const auto& atom : object->molecule->atoms()) pointIds_.push_back(atom.id);
    }
    if (pca) {
        std::vector<double> x(samples), y(samples);
        for (std::size_t i = 0; i < samples; ++i) {
            x[i] = result.values[2 * i];
            y[i] = result.values[2 * i + 1];
        }
        const std::array<double, 2> explained{result.values[2 * samples], result.values[2 * samples + 1]};
        std::vector<int> groups;
        if (groupCount > 0) {
            std::vector<std::size_t> counts(groupCount);
            groups.reserve(samples);
            for (std::size_t i = 0; i < samples; ++i) {
                const auto group = result.values[2 * samples + 2 + i];
                if (group < 1.0 || group > groupCount || std::floor(group) != group) {
                    pointIds_.clear();
                    status_->setText(QStringLiteral("Julia returned invalid cluster assignments."));
                    return;
                }
                groups.push_back(static_cast<int>(group));
                ++counts[groups.back() - 1];
            }
            QStringList sizes;
            for (int i = 0; i < groupCount; ++i) {
                sizes.push_back(QStringLiteral("%1 (%2)").arg(i + 1).arg(counts[i]));
            }
            summary += QStringLiteral(" : cluster sizes %1").arg(sizes.join(QStringLiteral(", ")));
            if (result.values.back() != 1.0) summary += QStringLiteral(" : clustering reached the iteration limit");
        }
        status_->setText(summary);
        plot_->setScatter(std::move(x), std::move(y), pointIds_, explained, std::move(groups));
        updateMarkers(*object);
        return;
    }
    const auto [min, max] = std::minmax_element(result.values.begin(), result.values.end());
    const auto mean = std::accumulate(result.values.begin(), result.values.end(), 0.0) / points;
    status_->setText(summary + QStringLiteral(" : min %1, mean %2, max %3 Å")
                                   .arg(number(*min), number(mean), number(*max)));
    plot_->setSeries(std::move(result.values), pointIds_, frames ? step : 1, frames ? frameCount : selectedCount, frames);
    updateMarkers(*object);
}

void TrajectoryAnalysisPanel::updateMarkers(const MolecularObject& object) {
    if (metric() == Metric::Rmsd) {
        plot_->setMarkers({object.frameIndex + 1});
        return;
    }
    if (metric() == Metric::Pca) {
        const auto frame = object.frameIndex + 1;
        const auto found = std::lower_bound(pointIds_.begin(), pointIds_.end(), frame);
        plot_->setMarkers(found != pointIds_.end() && *found == frame
            ? std::vector<std::size_t>{static_cast<std::size_t>(found - pointIds_.begin()) + 1}
            : std::vector<std::size_t>{});
        return;
    }
    const std::unordered_set<std::uint64_t> selected(selection_.begin(), selection_.end());
    std::vector<std::size_t> markers;
    if (!selected.empty()) {
        for (std::size_t i = 0; i < pointIds_.size(); ++i) {
            if (selected.contains(pointIds_[i])) markers.push_back(i + 1);
        }
    }
    plot_->setMarkers(std::move(markers));
}

void TrajectoryAnalysisPanel::activatePoint(std::size_t index) {
    const auto active = scene_->activeObjectId();
    const auto object = active ? scene_->object(*active) : std::nullopt;
    if (!object || object_ != active || trajectory_ != object->trajectory || index >= pointIds_.size()) return;
    if (metric() != Metric::Rmsf) {
        dispatcher_->setTrajectoryFrame(*active, pointIds_[index] - 1);
    } else {
        dispatcher_->selectAtom(*active, std::optional<std::uint64_t>{pointIds_[index]});
    }
}

} // namespace jlmol
