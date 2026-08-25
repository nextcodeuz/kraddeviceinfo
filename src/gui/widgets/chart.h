// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - realtime multi-series line chart widget
#pragma once

#include <QWidget>
#include <QColor>
#include <deque>
#include <QString>
#include <vector>

namespace krad {
namespace ui {

class RealtimeChart : public QWidget {
    Q_OBJECT
public:
    explicit RealtimeChart(QWidget* parent = nullptr);

    void addSeries(const QString& name, const QColor& color,
                   double max_hint = 100.0);
    void ensureSeriesCount(int n);                   // auto-name "core i"
    void clearSeries();                              // remove all series
    void pushValues(const std::vector<double>& v);   // aligned to series
    void setMaxPoints(int n);
    void setTitle(const QString& t);
    void clear();

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    struct Series {
        QString name;
        QColor color;
        double max_hint = 100.0;
        std::deque<double> values;
    };
    std::vector<Series> series_;
    int max_points_ = 90;
    QString title_;
};

} // namespace ui
} // namespace krad
