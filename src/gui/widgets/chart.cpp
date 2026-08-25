// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - realtime chart painting
#include "chart.h"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace krad {
namespace ui {

RealtimeChart::RealtimeChart(QWidget* parent) : QWidget(parent) {
    setMinimumSize(240, 120);
}

void RealtimeChart::addSeries(const QString& name, const QColor& color,
                              double max_hint) {
    Series s;
    s.name = name;
    s.color = color;
    s.max_hint = max_hint;
    series_.push_back(std::move(s));
}

void RealtimeChart::clearSeries() {
    series_.clear();
    update();
}

void RealtimeChart::ensureSeriesCount(int n) {
    static const QColor palette[] = {
        QColor("#4fc3f7"), QColor("#a78bfa"), QColor("#34d399"),
        QColor("#fbbf24"), QColor("#f472b6"), QColor("#60a5fa"),
        QColor("#fb923c"), QColor("#4ade80"), QColor("#e879f9"),
        QColor("#38bdf8"), QColor("#facc15"), QColor("#c084fc"),
        QColor("#2dd4bf"), QColor("#fb7185"), QColor("#93c5fd"),
        QColor("#fdba74"),
    };
    bool changed = false;
    while (int(series_.size()) < n && int(series_.size()) < 64) {
        Series s;
        s.name = QString("core %1").arg(series_.size());
        s.color = palette[series_.size() % 16];
        s.max_hint = 100;
        series_.push_back(std::move(s));
        changed = true;
    }
    if (changed) update();
}

void RealtimeChart::pushValues(const std::vector<double>& v) {
    for (size_t i = 0; i < series_.size() && i < v.size(); ++i) {
        series_[i].values.push_back(v[i]);
        while (int(series_[i].values.size()) > max_points_)
            series_[i].values.pop_front();
    }
    update();
}

void RealtimeChart::setMaxPoints(int n) { max_points_ = n; }
void RealtimeChart::setTitle(const QString& t) { title_ = t; update(); }
void RealtimeChart::clear() {
    for (auto& s : series_) s.values.clear();
    update();
}

void RealtimeChart::resizeEvent(QResizeEvent*) { update(); }

void RealtimeChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRectF area = rect().adjusted(8, 8, -10, -22);

    // frame + horizontal gridlines (4 lines)
    QPen grid_pen(palette().color(QPalette::AlternateBase), 1);
    p.setPen(grid_pen);
    p.setBrush(palette().color(QPalette::Base));
    p.drawRoundedRect(area, 6, 6);
    for (int i = 1; i <= 3; ++i) {
        double y = area.top() + area.height() * i / 4.0;
        QPen gp(palette().color(QPalette::AlternateBase), 1, Qt::DotLine);
        p.setPen(gp);
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }

    // determine y-scale across series
    double ymax = 0;
    for (auto& s : series_) {
        ymax = std::max(ymax, s.max_hint);
        for (double v : s.values)
            if (v >= 0 && v > ymax * 0.999) ymax = v;   // soft autoscale
    }
    if (ymax <= 0) ymax = 100;

    // series lines with gradient fill under first series only
    bool first_series = true;
    for (auto& s : series_) {
        if (s.values.empty()) continue;
        QPainterPath line;
        const size_t n = s.values.size();
        const double dx =
            n > 1 ? area.width() / double(max_points_ - 1) : 0;
        const double x0 = area.right() - double(n - 1) * dx;

        bool started = false;
        for (size_t i = 0; i < n; ++i) {
            double val = std::clamp(s.values[i], 0.0, ymax);
            double x = x0 + double(i) * dx;
            double y = area.bottom() -
                       (val / ymax) * area.height();
            if (!started) { line.moveTo(x, y); started = true; }
            else          { line.lineTo(x, y); }
        }

        if (first_series) {
            QPainterPath fill = line;
            fill.lineTo(x0 + double(n - 1) * dx, area.bottom());
            fill.lineTo(x0, area.bottom());
            fill.closeSubpath();
            QColor cfill = s.color;
            cfill.setAlpha(38);
            QLinearGradient g(0, area.top(), 0, area.bottom());
            cfill.setAlpha(60);  g.setColorAt(0, cfill);
            cfill.setAlpha(5);   g.setColorAt(1, cfill);
            p.fillPath(fill, g);
            first_series = false;
        }

        QPen pen(s.color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.drawPath(line);
    }

    // max label
    p.setPen(palette().color(QPalette::PlaceholderText));
    QFont f = font();
    f.setPointSizeF(7.5);
    p.setFont(f);
    p.drawText(QRectF(area.right() - 54, area.top() + 2, 50, 14),
               Qt::AlignRight,
               ymax >= 10 ? QString::number(std::lround(ymax)) :
                            QString::number(ymax, 'f', 1));

    // legend
    if (!series_.empty()) {
        double lx = area.left() + 2;
        const double ly = height() - 12;
        for (auto& s : series_) {
            QRectF swatch(lx, ly - 7, 9, 9);
            p.fillRect(swatch, s.color);
            QRectF text_r(lx + 13, ly - 11, 90, 16);
            p.drawText(text_r, Qt::AlignVCenter | Qt::AlignLeft, s.name);
            lx += 20 + QFontMetrics(font()).horizontalAdvance(s.name);
        }
    }

    if (!title_.isEmpty()) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(rect().adjusted(0, 2, 0, 0), Qt::AlignTop | Qt::AlignHCenter,
                   title_);
    }
}

} // namespace ui
} // namespace krad
