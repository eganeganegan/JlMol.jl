#pragma once

#include "core/MolecularScene.h"

#include <QWidget>

class QLabel;
class QSlider;
class QSpinBox;
class QTimer;
class QToolButton;

namespace jlmol {

class CommandDispatcher;

class TrajectoryPanel final : public QWidget {
public:
    TrajectoryPanel(std::shared_ptr<MolecularScene> scene, CommandDispatcher* dispatcher, QWidget* parent);
    void refresh();

private:
    void seek(int oneBasedFrame);
    void advance();

    std::shared_ptr<MolecularScene> scene_;
    CommandDispatcher* dispatcher_;
    std::optional<ObjectId> object_;
    std::shared_ptr<const Trajectory> trajectory_;
    QTimer* timer_{};
    QToolButton* play_{};
    QSpinBox* rate_{};
    QSlider* slider_{};
    QSpinBox* frame_{};
    QLabel* summary_{};
};

} // namespace jlmol
