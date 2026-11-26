// SPDX-License-Identifier: LGPL-2.0-or-later
// Breeze NG Aurora: one way to draw a ring (a dial's groove, a gauge), shared by the widget style and the Kirigami plugin.
// Angles as QPainter::drawArc has them: degrees, counter-clockwise from three o'clock; a negative span runs clockwise.
#pragma once

#include <QConicalGradient>
#include <QPainter>
#include <cmath>

namespace BreezeNg
{
enum RingStyle { RingPlain = 0, RingGradient = 1, RingGlow = 2, RingTicks = 3 };

// rect: the ring's outer bounds; width: its thickness; start, span: the whole track; value: 0..1 of it; first, second:
// the accent and Aurora's second light (the gradient runs along the filled part: both colours show at any value)
inline void paintAuroraRing(QPainter *painter, const QRectF &rect, qreal width, qreal start, qreal span, qreal value, int style, const QColor &first,
                            const QColor &second, const QColor &track)
{
    value = qBound(0.0, value, 1.0);
    // (inside rect with all that is drawn: the glow's halo, and the ticks' outer ends with their round caps)
    const qreal tickPen = qMax(1.6, width * 0.42);
    const qreal margin = style == RingTicks ? width * 0.45 + tickPen / 2 : width / 2 + (style == RingGlow ? width * 0.7 : 0.0);
    const QRectF arc = rect.adjusted(margin, margin, -margin, -margin);
    if (arc.width() <= 2 || arc.height() <= 2) {
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(Qt::NoBrush);
    QBrush paint(first);
    if (style != RingPlain) {
        QConicalGradient gradient(arc.center(), start);
        // (the gradient runs along the filled part, from the first colour at its start to the second at its end: along
        // the whole track a low value showed one colour only, and read as a plain ring)
        const qreal part = qMin(1.0, std::abs(span) / 360.0) * qMax(value, 0.12);
        // (the first colour on both sides of the start, so that the round cap there is not the other end's colour)
        if (span < 0) { // clockwise: the track covers the end of the gradient's turn
            gradient.setColorAt(0.0, first);
            gradient.setColorAt(qMax(0.04, 1.0 - part), second);
            gradient.setColorAt(1.0, first);
        } else {
            gradient.setColorAt(0.0, first);
            gradient.setColorAt(qMin(0.96, part), second);
            gradient.setColorAt(1.0, first);
        }
        paint = QBrush(gradient);
    }
    const QPointF centre = arc.center();
    const qreal radius = arc.width() / 2;
    auto at = [&](qreal degrees, qreal r) {
        const qreal t = degrees * M_PI / 180.0;
        return QPointF(centre.x() + r * std::cos(t), centre.y() - r * std::sin(t));
    };
    if (style == RingTicks) {
        const int count = qBound(12, int(std::abs(span) / 360.0 * 2 * M_PI * radius / qMax(4.0, width * 1.35)), 72);
        const qreal step = span / count;
        for (int i = 0; i < count; ++i) {
            const qreal angle = start + step * (i + 0.5);
            const bool on = (i + 0.5) / count <= value && value > 0;
            painter->setPen(QPen(on ? paint : QBrush(track), tickPen, Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(at(angle, radius - width * 0.75), at(angle, radius + width * 0.45));
        }
        painter->restore();
        return;
    }
    painter->setPen(QPen(track, width, Qt::SolidLine, Qt::RoundCap));
    painter->drawArc(arc, qRound(start * 16), qRound(span * 16));
    if (value > 0) {
        const int from = qRound(start * 16), length = qRound(span * value * 16);
        // (the caller's opacity is kept: the halo is a part of it)
        const qreal opacity = painter->opacity();
        if (style == RingGlow) {
            for (const auto &halo : {std::pair<qreal, qreal>{2.5, 0.10}, {1.9, 0.14}, {1.45, 0.20}}) {
                painter->setOpacity(opacity * halo.second);
                painter->setPen(QPen(paint, width * halo.first, Qt::SolidLine, Qt::RoundCap));
                painter->drawArc(arc, from, length);
            }
            painter->setOpacity(opacity);
        }
        painter->setPen(QPen(paint, width, Qt::SolidLine, Qt::RoundCap));
        painter->drawArc(arc, from, length == 0 ? (span < 0 ? -1 : 1) : length);
        if (style == RingGlow) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(255, 255, 255, 235));
            painter->drawEllipse(at(start + span * value, radius), width * 0.30, width * 0.30);
        }
    }
    painter->restore();
}
}
