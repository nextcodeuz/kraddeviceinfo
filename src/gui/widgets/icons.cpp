// krad.device.info — KradDeviceInfo
// Copyright (c) 2026 Krad. Licensed under the MIT License.
// This file is part of the KradDeviceInfo source distribution.
// See the LICENSE file in the project root for the full text.

// krad.device.info - icons painted at runtime
#include "../icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <cmath>
#include <map>

namespace krad {
namespace ui {

static QIcon paint_icon(int kind) {
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    QColor bg("#2563eb");
    QColor fg("#ffffff");

    QRectF r(6, 6, 52, 52);
    QPainterPath rr;
    rr.addRoundedRect(r, 14, 14);
    p.fillPath(rr, bg);

    p.setPen(QPen(fg, 4.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    switch (kind) {
    case 0: { // overview - speedometer
        QRectF dial(16, 16, 32, 32);
        p.drawArc(dial, 225 * 16, -270 * 16);
        p.drawLine(QPointF(32, 34), QPointF(41, 23));
    } break;
    case 1: { // cpu chip
        p.drawRoundedRect(18, 18, 28, 28, 5, 5);
        p.drawRoundedRect(26, 26, 12, 12, 2, 2);
        for (int i = 0; i < 3; ++i) {
            p.drawLine(QPointF(24, 12 + i * 3), QPointF(24, 17));
            p.drawLine(QPointF(40, 12 + i * 3), QPointF(40, 17));
            p.drawLine(QPointF(24, 47), QPointF(24, 52 - 0));
            p.drawLine(QPointF(40, 47), QPointF(40, 52));
            p.drawLine(QPointF(12, 24 + i * 8), QPointF(17, 24 + i * 8));
            p.drawLine(QPointF(47, 24 + i * 8), QPointF(52, 24 + i * 8));
        }
    } break;
    case 2: { // memory stick
        p.drawRoundedRect(12, 22, 40, 20, 3, 3);
        for (int i = 0; i < 5; ++i)
            p.drawLine(QPointF(17 + i * 7, 27), QPointF(17 + i * 7, 37));
        for (int i = 0; i < 6; ++i)
            p.drawLine(QPointF(15 + i * 6.5, 42), QPointF(15 + i * 6.5, 48));
    } break;
    case 3: { // gpu card
        p.drawRoundedRect(10, 20, 44, 24, 5, 5);
        p.drawRect(16, 26, 12, 12);
        p.drawEllipse(36, 30, 9, 9);
        p.drawLine(QPointF(10, 50), QPointF(54, 50));
    } break;
    case 4: { // disk drive
        p.drawRoundedRect(12, 18, 40, 28, 5, 5);
        p.drawEllipse(38, 36, 8, 5);
        p.drawLine(QPointF(18, 40), QPointF(30, 40));
    } break;
    case 5: { // network globe
        p.drawEllipse(15, 15, 34, 34);
        p.drawEllipse(25, 15, 14, 34);
        p.drawLine(QPointF(15, 32), QPointF(49, 32));
    } break;
    case 6: { // monitor
        p.drawRoundedRect(12, 14, 40, 26, 4, 4);
        p.drawLine(QPointF(32, 40), QPointF(32, 46));
        p.drawLine(QPointF(22, 50), QPointF(42, 50));
    } break;
    case 7: { // software boxes
        p.drawRoundedRect(13, 13, 17, 17, 4, 4);
        p.drawRoundedRect(34, 13, 17, 17, 4, 4);
        p.drawRoundedRect(13, 34, 17, 17, 4, 4);
        p.fillRect(QRectF(36, 36, 13, 13), fg);
    } break;
    case 8: { // benchmark gauge/flask
        p.drawLine(QPointF(26, 12), QPointF(26, 28));
        p.drawLine(QPointF(38, 12), QPointF(38, 28));
        p.drawLine(QPointF(26, 28), QPointF(16, 48));
        p.drawLine(QPointF(38, 28), QPointF(48, 48));
        p.drawLine(QPointF(16, 48), QPointF(48, 48));
        p.drawLine(QPointF(21, 39), QPointF(43, 39));
    } break;
    case 10: { // online globe with signal
        p.drawEllipse(15, 15, 34, 34);
        p.drawEllipse(25, 15, 14, 34);
        p.drawLine(QPointF(15, 32), QPointF(49, 32));
        p.drawArc(QRectF(8, 8, 48, 48), 30 * 16, 60 * 16);
        p.drawArc(QRectF(8, 8, 48, 48), 210 * 16, 60 * 16);
    } break;
    case 9: { // report document
        p.drawRoundedRect(16, 12, 32, 40, 5, 5);
        for (int i = 0; i < 4; ++i)
            p.drawLine(QPointF(22, 21 + i * 8), QPointF(42, 21 + i * 8));
    } break;
    default: break;
    }

    return QIcon(pm);
}

QIcon Icons::get(Icon id) {
    static std::map<int, QIcon> cache;
    int k = static_cast<int>(id);
    auto it = cache.find(k);
    if (it == cache.end())
        it = cache.emplace(k, paint_icon(k)).first;
    return it->second;
}

QIcon makeAppIcon() { return paint_icon(0); }

} // namespace ui
} // namespace krad
