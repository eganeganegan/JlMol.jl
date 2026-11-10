#include "gui/TrajectoryPanel.h"

#include "commands/CommandDispatcher.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <limits>

namespace jlmol {

TrajectoryPanel::TrajectoryPanel(std::shared_ptr<MolecularScene> scene, CommandDispatcher* dispatcher, QWidget* parent)
    : QWidget(parent), scene_(std::move(scene)), dispatcher_(dispatcher) {
    auto* layout = new QVBoxLayout(this);
    auto* controls = new QHBoxLayout;
    layout->addLayout(controls);
    play_ = new QToolButton(this);
    play_->setObjectName(QStringLiteral("TrajectoryPlay"));
    play_->setCheckable(true);
    play_->setText(QStringLiteral("Play"));
    controls->addWidget(play_);
    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setObjectName(QStringLiteral("TrajectorySlider"));
    slider_->setAccessibleName(QStringLiteral("Trajectory timeline"));
    slider_->setTracking(false);
    controls->addWidget(slider_, 1);
    controls->addWidget(new QLabel(QStringLiteral("Frame"), this));
    frame_ = new QSpinBox(this);
    frame_->setObjectName(QStringLiteral("TrajectoryFrame"));
    frame_->setAccessibleName(QStringLiteral("Trajectory frame number"));
    frame_->setKeyboardTracking(false);
    controls->addWidget(frame_);
    rate_ = new QSpinBox(this);
    rate_->setRange(1, 60);
    rate_->setValue(12);
    rate_->setSuffix(QStringLiteral(" fps"));
    rate_->setAccessibleName(QStringLiteral("Playback speed"));
    controls->addWidget(rate_);
    summary_ = new QLabel(this);
    summary_->setTextFormat(Qt::PlainText);
    layout->addWidget(summary_);

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &TrajectoryPanel::advance);
    connect(slider_, &QSlider::valueChanged, this, &TrajectoryPanel::seek);
    connect(frame_, &QSpinBox::valueChanged, this, &TrajectoryPanel::seek);
    connect(rate_, &QSpinBox::valueChanged, this, [this](int fps) { timer_->setInterval(1000 / fps); });
    connect(play_, &QToolButton::toggled, this, [this](bool playing) {
        play_->setText(playing ? QStringLiteral("Pause") : QStringLiteral("Play"));
        if (!playing) {
            timer_->stop();
            return;
        }
        if (frame_->value() == frame_->maximum()) seek(1);
        if (play_->isChecked()) timer_->start(1000 / rate_->value());
    });
    refresh();
}

void TrajectoryPanel::refresh() {
    const auto active = scene_->activeObjectId();
    const auto object = active ? scene_->object(*active) : std::nullopt;
    const auto trajectory = object ? object->trajectory : nullptr;
    if (object_ != active || trajectory_ != trajectory) play_->setChecked(false);
    object_ = active;
    trajectory_ = trajectory;
    const auto count = trajectory ? trajectory->frameCount() : 0;
    const bool available = count > 0 && count <= static_cast<std::size_t>(std::numeric_limits<int>::max());
    const int maximum = available ? static_cast<int>(count) : 1;
    const int current = available ? static_cast<int>(object->frameIndex + 1) : 1;
    const QSignalBlocker blockSlider(slider_);
    const QSignalBlocker blockFrame(frame_);
    slider_->setRange(1, maximum);
    frame_->setRange(1, maximum);
    slider_->setValue(current);
    frame_->setValue(current);
    slider_->setEnabled(available && count > 1);
    frame_->setEnabled(available && count > 1);
    play_->setEnabled(available && count > 1);
    rate_->setEnabled(available && count > 1);
    if (!trajectory) {
        summary_->setText(QStringLiteral("Load a structure, then use File > Attach Trajectory."));
        return;
    }
    if (!available) {
        summary_->setText(QStringLiteral("Too many frames for the timeline. Use Julia to seek by frame."));
        return;
    }
    auto text = QStringLiteral("%1 : frame %2 of %3")
        .arg(QString::fromStdString(trajectory->path().filename().string())).arg(current).arg(maximum);
    if (object->frameMetadata.timePs) text += QStringLiteral(" : %1 ps").arg(*object->frameMetadata.timePs, 0, 'g', 7);
    if (std::any_of(object->frameMetadata.cell.begin(), object->frameMetadata.cell.end(),
                    [](double value) { return value != 0.0; })) text += QStringLiteral(" : periodic cell");
    summary_->setText(text);
}

int TrajectoryPanel::playbackRate() const { return rate_->value(); }

void TrajectoryPanel::pause() { play_->setChecked(false); }

void TrajectoryPanel::restorePlaybackRate(int rate) {
    pause();
    rate_->setValue(rate);
}

void TrajectoryPanel::seek(int oneBasedFrame) {
    if (!object_ || !trajectory_) return;
    if (!dispatcher_->setTrajectoryFrame(*object_, static_cast<std::size_t>(oneBasedFrame - 1))) {
        play_->setChecked(false);
        refresh();
    }
}

void TrajectoryPanel::advance() {
    refresh();
    if (!play_->isChecked()) return;
    if (frame_->value() < frame_->maximum()) seek(frame_->value() + 1);
    if (frame_->value() == frame_->maximum()) play_->setChecked(false);
}

} // namespace jlmol
