#pragma once

#include "../appcontroller.hpp"

#include <QWidget>

namespace sc {

// Small close button positioned at the top-right of the capture region.
// Remains interactive even when CaptureWindow is click-through.
class CloseButton : public QWidget {
    Q_OBJECT

public:
    explicit CloseButton(QWidget* parent = nullptr);

signals:
    void closeRequested();

public slots:
    void onRegionChanged(const sc::CaptureRegion& region);

protected:
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    static constexpr int kSize = 32;

    bool m_hovered = false;
    bool m_pressed = false;
};

} // namespace sc
