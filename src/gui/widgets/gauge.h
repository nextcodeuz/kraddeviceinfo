// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - circular gauge widget
#pragma once

#include <QWidget>
#include <QString>

namespace krad {
namespace ui {

class Gauge : public QWidget {
    Q_OBJECT
public:
    explicit Gauge(QWidget* parent = nullptr);
    void setValue(double pct, double max = 100.0);
    void setLabel(const QString& text);
    void setSubLabel(const QString& text);

    QSize minimumSizeHint() const override { return {110, 110}; }
    QSize sizeHint() const override { return {150, 150}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    double value_ = 0;      // current
    double max_ = 100;
    QString label_, sub_;
};

} // namespace ui
} // namespace krad
