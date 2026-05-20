#include "busy_spinner.h"

#include <QColor>
#include <QPainter>
#include <QPalette>
#include <QTimer>

#include <cmath>

namespace {

constexpr qreal kTau = 6.28318530717958647692;

} // namespace

BusySpinner::BusySpinner(QWidget *parent)
    : QWidget(parent)
    , timer(new QTimer(this))
{
    setFixedSize(18, 18);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();

    timer->setInterval(80);
    connect(timer, &QTimer::timeout, this, [this]() {
        phase = (phase + 1) % kLineCount;
        update();
    });
}

void BusySpinner::start()
{
    if (timer->isActive()) {
        return;
    }

    phase = 0;
    timer->start();
    show();
    update();
}

void BusySpinner::stop()
{
    timer->stop();
    hide();
}

void BusySpinner::paintEvent(QPaintEvent *)
{
    if (!timer->isActive()) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    const QColor baseColor(66, 133, 244);

    const qreal side = static_cast<qreal>(qMin(width(), height()));
    const qreal outerRadius = side * 0.46;
    const qreal dotSize = side * 0.10;
    const QPointF center(width() / 2.0, height() / 2.0);

    for (int i = 0; i < kLineCount; ++i) {
        QColor dotColor = baseColor;
        const int trail = (i - phase + kLineCount) % kLineCount;
        dotColor.setAlphaF(0.18 + (static_cast<qreal>(kLineCount - trail) / kLineCount) * 0.82);
        painter.setBrush(dotColor);

        const qreal angle = (static_cast<qreal>(i) / kLineCount) * kTau;
        const QPointF point(center.x() + std::cos(angle) * outerRadius,
                            center.y() + std::sin(angle) * outerRadius);
        painter.drawEllipse(point, dotSize, dotSize);
    }
}
