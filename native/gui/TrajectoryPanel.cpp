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
    connect(dispatcher_, &CommandDispatcher::trajectoryFrameRequestChanged, this, &TrajectoryPanel::refresh);
    connect(dispatcher_, &CommandDispatcher::trajectoryFrameRequestFailed, this, &TrajectoryPanel::pause);
    connect(timer_, &QTimer::timeout, this, &TrajectoryPanel::advance);
    connect(slider_, &QSlider::valueChanged, this, &TrajectoryPanel::seek);
    connect(slider_, &QSlider::sliderReleased, this, &TrajectoryPanel::refresh);
    connect(frame_, &QSpinBox::valueChanged, this, &TrajectoryPanel::seek);
    connect(rate_, &QSpinBox::valueChanged, this, [this](int fps) { timer_->setInterval(1000 / fps); });
    connect(play_, &QToolButton::toggled, this, [this](bool playing) {
        play_->setText(playing ? QStringLiteral("Pause") : QStringLiteral("Play"));
        if (!playing) {
            timer_->stop();
            return;
        }
        const auto object = object_ ? scene_->object(*object_) : std::nullopt;
        if (object && object->frameIndex + 1 == static_cast<std::size_t>(frame_->maximum()) &&
            !dispatcher_->requestedTrajectoryFrame(*object)) seek(1);
        if (play_->isChecked()) timer_->start(1000 / rate_->value());
    });
    connect(play_, &QToolButton::clicked, this, [this](bool playing) { if (!playing) pause(); });
    refresh();
}

void TrajectoryPanel::refresh() {
    const auto active = scene_->activeObjectId();
    const auto object = active ? scene_->object(*active) : std::nullopt;
    const auto trajectory = object ? object->trajectory : nullptr;
    const auto previous = object_;
    const bool contextChanged = object_ != active || trajectory_ != trajectory;
    object_ = active;
    trajectory_ = trajectory;
    if (contextChanged) {
        play_->setChecked(false);
        if (previous) dispatcher_->cancelTrajectoryFrameRequest(*previous);
    }
    const auto count = trajectory ? trajectory->frameCount() : 0;
    const bool available = count > 0 && count <= static_cast<std::size_t>(std::numeric_limits<int>::max());
    const int maximum = available ? static_cast<int>(count) : 1;
    const int current = available ? static_cast<int>(object->frameIndex + 1) : 1;
    const auto requested = available ? dispatcher_->requestedTrajectoryFrame(*object) : std::nullopt;
    const int controlFrame = requested ? static_cast<int>(*requested + 1) : current;
    const QSignalBlocker blockSlider(slider_);
    const QSignalBlocker blockFrame(frame_);
    slider_->setRange(1, maximum);
    frame_->setRange(1, maximum);
    if (!slider_->isSliderDown()) slider_->setValue(controlFrame);
    frame_->setValue(controlFrame);
    slider_->setEnabled(available && count > 1);
    frame_->setEnabled(available && count > 1);
    play_->setEnabled(available && count > 1);
    rate_->setEnabled(available && count > 1);
    if (!requested && current == maximum) play_->setChecked(false);
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
    if (requested) text += QStringLiteral(" - Loading frame %1").arg(controlFrame);
    summary_->setText(text);
}

int TrajectoryPanel::playbackRate() const { return rate_->value(); }

void TrajectoryPanel::pause() {
    play_->setChecked(false);
    if (object_) dispatcher_->cancelTrajectoryFrameRequest(*object_);
}

void TrajectoryPanel::restorePlaybackRate(int rate) {
    pause();
    rate_->setValue(rate);
}

void TrajectoryPanel::seek(int oneBasedFrame) {
    if (!object_ || !trajectory_) return;
    dispatcher_->requestTrajectoryFrame(*object_, static_cast<std::size_t>(oneBasedFrame - 1));
}

void TrajectoryPanel::advance() {
    if (!play_->isChecked() || !object_) return;
    const auto object = scene_->object(*object_);
    if (!object || !object->trajectory || dispatcher_->requestedTrajectoryFrame(*object)) return;
    const auto current = static_cast<int>(object->frameIndex + 1);
    if (current < frame_->maximum()) seek(current + 1);
    else play_->setChecked(false);
}

} // namespace jlmol
