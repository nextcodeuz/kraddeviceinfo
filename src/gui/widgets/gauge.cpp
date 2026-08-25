// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - gauge painting
#include "gauge.h"

#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace krad {
namespace ui {

Gauge::Gauge(QWidget* parent) : QWidget(parent) {
    setMinimumSize(120, 130);
}

void Gauge::setValue(double pct, double max) {
    value_ = max > 0 ? pct : 0;
    max_ = max > 0 ? max : 1.0;
    update();
}
void Gauge::setLabel(const QString& t)    { label_ = t; update(); }
void Gauge::setSubLabel(const QString& t) { sub_ = t; update(); }

void Gauge::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF area = rect();
    const double side = qMin(area.width(), area.height());
    QRectF ring(area.center().x() - side * 0.36,
                area.center().y() - side * 0.34,
                side * 0.72, side * 0.72);

    const double frac = std::clamp(value_ / max_, 0.0, 1.0);
    const int start_angle = 225 * 16;
    const int span        = -270 * 16;

    QPen pen(palette().color(QPalette::AlternateBase), side * 0.085,
             Qt::SolidLine, Qt::RoundCap);
    p.setPen(pen);
    p.drawArc(ring, start_angle, span);

    // colored arc: green -> amber -> red by load
    QColor c = frac < 0.55 ? QColor("#3fb950")
             : frac < 0.8  ? QColor("#f0b429")
                           : QColor("#ef5350");
    if (frac > 0.003) {
        QPen vpen(c, side * 0.085, Qt::SolidLine, Qt::RoundCap);
        p.setPen(vpen);
        p.drawArc(ring, start_angle, int(span * frac));
    }

    // center text: percentage big
    QFont f = font();
    f.setBold(true);
    f.setPointSizeF(side * 0.115);
    p.setFont(f);
    p.setPen(palette().color(QPalette::WindowText));
    double shown = std::lround(frac * 100);
    QString big;
    if (!label_.isEmpty() && max_ != 100.0)
        big = label_;                              // non-percent gauges
    else
        big = QString::number(shown) + "%";
    p.drawText(ring.adjusted(-20, -6, -20, 6), Qt::AlignCenter, big);

    if (!sub_.isEmpty()) {
        QFont sf = font();
        sf.setPointSizeF(std::max<double>(7.5, side * 0.062));
        p.setFont(sf);
        p.setPen(palette().color(QPalette::PlaceholderText));
        QRectF sr(0, ring.bottom(), width(), height() - ring.bottom());
        p.drawText(sr, Qt::AlignHCenter | Qt::AlignTop, sub_);
    }
}

} // namespace ui
} // namespace krad
