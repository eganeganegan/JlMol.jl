#pragma once

#include <QImage>
#include <QWidget>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace jlmol {

class ContactMapWidget final : public QWidget {
    Q_OBJECT
public:
    explicit ContactMapWidget(QWidget* parent);
    void setMatrix(QImage matrix, std::vector<std::uint64_t> atomIds);
    void clear();
    void setSelectedAtoms(std::span<const std::uint64_t> atomIds);
    [[nodiscard]] QPointF cellCenter(int row, int column) const;

signals:
    void pairActivated(std::uint64_t first, std::uint64_t second);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool event(QEvent* event) override;

private:
    [[nodiscard]] std::optional<QPoint> cellAt(QPointF point) const;
    QImage matrix_;
    std::vector<std::uint64_t> atomIds_;
    std::vector<std::uint64_t> selectedIds_;
    std::vector<int> selected_;
};

} // namespace jlmol
