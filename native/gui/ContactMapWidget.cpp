#include "gui/ContactMapWidget.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>
#include <unordered_set>

namespace jlmol {

ContactMapWidget::ContactMapWidget(QWidget* parent) : QWidget(parent) {
    setAccessibleName(QStringLiteral("Atom contact map"));
}

void ContactMapWidget::setMatrix(QImage matrix, std::vector<std::uint64_t> atomIds) {
    matrix_ = std::move(matrix);
    atomIds_ = std::move(atomIds);
    selectedIds_.clear();
    selected_.clear();
    // Keep every cell reachable, even for maps larger than the scroll viewport.
    const int side = std::max(384, matrix_.width());
    setFixedSize(side, side);
    update();
}

void ContactMapWidget::clear() {
    matrix_ = {};
    atomIds_.clear();
    selectedIds_.clear();
    selected_.clear();
    update();
}

void ContactMapWidget::setSelectedAtoms(std::span<const std::uint64_t> atomIds) {
    if (std::equal(selectedIds_.begin(), selectedIds_.end(), atomIds.begin(), atomIds.end())) return;
    selectedIds_.assign(atomIds.begin(), atomIds.end());
    const std::unordered_set<std::uint64_t> lookup(atomIds.begin(), atomIds.end());
    std::vector<int> next;
    for (int i = 0; i < static_cast<int>(atomIds_.size()); ++i) {
        if (lookup.contains(atomIds_[i])) next.push_back(i);
    }
    if (selected_ != next) {
        selected_ = std::move(next);
        update();
    }
}

QPointF ContactMapWidget::cellCenter(int row, int column) const {
    const auto scale = static_cast<double>(width()) / std::max(1, matrix_.width());
    return {(column + 0.5) * scale, (row + 0.5) * scale};
}

std::optional<QPoint> ContactMapWidget::cellAt(QPointF point) const {
    if (matrix_.isNull() || point.x() < 0 || point.x() >= width() || point.y() < 0 || point.y() >= height()) {
        return std::nullopt;
    }
    return QPoint(static_cast<int>(point.x() * matrix_.width() / width()),
                  static_cast<int>(point.y() * matrix_.height() / height()));
}

void ContactMapWidget::paintEvent(QPaintEvent*) {
    if (matrix_.isNull()) return;
    QPainter painter(this);
    painter.drawImage(rect(), matrix_);
    const auto cellSize = static_cast<double>(width()) / matrix_.width();
    const QColor axis(236, 140, 35, 120);
    for (const int index : selected_) {
        const auto start = index * cellSize;
        painter.fillRect(QRectF(start, 0, cellSize, height()), axis);
        painter.fillRect(QRectF(0, start, width(), cellSize), axis);
    }
    if (selected_.size() == 2 && cellSize >= 3) {
        painter.setPen(QPen(QColor(109, 60, 12), 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(selected_[0] * cellSize, selected_[1] * cellSize, cellSize, cellSize));
        painter.drawRect(QRectF(selected_[1] * cellSize, selected_[0] * cellSize, cellSize, cellSize));
    }
}

void ContactMapWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (const auto cell = cellAt(event->position())) {
            emit pairActivated(atomIds_[cell->y()], atomIds_[cell->x()]);
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

bool ContactMapWidget::event(QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        const auto* help = static_cast<QHelpEvent*>(event);
        if (const auto cell = cellAt(help->pos())) {
            QToolTip::showText(help->globalPos(), QStringLiteral("Atom %1 × atom %2")
                .arg(atomIds_[cell->y()]).arg(atomIds_[cell->x()]), this);
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QWidget::event(event);
}

} // namespace jlmol
