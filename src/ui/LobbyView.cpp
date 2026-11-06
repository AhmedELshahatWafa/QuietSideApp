#include "LobbyView.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QFont>
#include <QFontMetrics>
#include <QColor>
#include <QPen>
#include <QBrush>

LobbyView::LobbyView(QWidget *parent) : QWidget(parent) {
    setMinimumSize(360, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void LobbyView::setDarkTheme(bool darkTheme) {
    m_darkTheme = darkTheme;
    update();
}

void LobbyView::setRooms(const QList<Room> &rooms) {
    m_rooms = rooms;
    update();
}

void LobbyView::setSessions(const QList<LobbySession> &sessions) {
    m_sessions = sessions;
    update();
}

void LobbyView::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    m_roomRects.clear();
    m_avatarRects.clear();

    const qreal w = width();
    const qreal h = height();
    const qreal marginX = qMax<qreal>(10, w * 0.035);
    const qreal marginY = qMax<qreal>(10, h * 0.045);
    const qreal gap = qMax<qreal>(8, w * 0.015);
    const qreal mapWidth = w - marginX * 2;
    const qreal mapHeight = h - marginY * 2;

    QMap<QString, QRect> roomLayouts;
    roomLayouts.insert("Balcony", QRect(
        qRound(marginX), qRound(marginY), qRound(mapWidth * 0.86), qRound(mapHeight * 0.12)));
    roomLayouts.insert("Main Room", QRect(
        qRound(marginX), qRound(marginY + mapHeight * 0.22),
        qRound(mapWidth * 0.49), qRound(mapHeight * 0.43)));
    roomLayouts.insert("Small Room", QRect(
        qRound(marginX + mapWidth * 0.52), qRound(marginY + mapHeight * 0.23),
        qRound(mapWidth * 0.33), qRound(mapHeight * 0.32)));
    roomLayouts.insert("Lounge", QRect(
        qRound(marginX), qRound(marginY + mapHeight * 0.67),
        qRound(mapWidth * 0.49), qRound(mapHeight * 0.32)));

    QList<Room> otherRooms;
    for (const Room &room : m_rooms) {
        if (!roomLayouts.contains(room.roomName)) otherRooms.append(room);
    }
    if (!otherRooms.isEmpty()) {
        const int columns = qMax(1, static_cast<int>(qCeil(qSqrt(otherRooms.size()))));
        const int rows = (otherRooms.size() + columns - 1) / columns;
        const qreal cellWidth = mapWidth / columns;
        const qreal cellHeight = mapHeight / rows;
        for (int i = 0; i < otherRooms.size(); ++i) {
            const int column = i % columns;
            const int row = i / columns;
            roomLayouts.insert(otherRooms[i].roomName, QRect(
                qRound(marginX + column * cellWidth + gap / 2),
                qRound(marginY + row * cellHeight + gap / 2),
                qRound(cellWidth - gap), qRound(cellHeight - gap)));
        }
    }

    for (const Room &r : m_rooms) {
        QRect rect = roomLayouts.value(r.roomName);
        if (rect.isEmpty()) {
            rect = QRect(r.x, r.y, r.width, r.height);
        }
        m_roomRects[r.id] = rect;

        // Room background
        QColor bg;
        QColor border;
        QColor title;
        QColor secondary;
        if (m_darkTheme) {
            bg = (r.roomName == "Lounge") ? QColor("#3b493d") : QColor("#343f4a");
            border = QColor("#786044");
            title = QColor("#f3e8d8");
            secondary = QColor("#c9b89f");
        } else {
            bg = (r.roomName == "Lounge") ? QColor("#ead9b9") : QColor("#f8edda");
            border = QColor("#a77a45");
            title = QColor("#3b2415");
            secondary = QColor("#704a2c");
        }
        p.setBrush(bg);
        p.setPen(QPen(border, 2));
        p.drawRoundedRect(rect, 14, 14);

        // Room title and capacity
        QFont f = p.font();
        f.setBold(true);
        f.setPointSize(qBound(10, qRound(qMin(rect.width() / 28.0, rect.height() / 5.0)), 18));
        QFont f2 = p.font();
        f2.setBold(false);
        f2.setPointSize(qMax(9, f.pointSize() - 2));
        const QString capacityText = QString("cap: %1").arg(r.capacity);
        const QFontMetrics capacityMetrics(f2);
        const int textLeft = rect.x() + 10;
        const int textTop = rect.y() + 8;
        const int capacityRight = rect.right() - 10;
        const int capacityLeft = capacityRight - capacityMetrics.horizontalAdvance(capacityText);
        const int titleMaxWidth = qMax(0, capacityLeft - textLeft - 8);

        p.setPen(title);
        while (f.pointSize() > 10
               && QFontMetrics(f).horizontalAdvance(r.roomName) > titleMaxWidth) {
            f.setPointSize(f.pointSize() - 1);
        }
        p.setFont(f);
        const QFontMetrics titleMetrics(f);
        p.drawText(QRect(textLeft, textTop, titleMaxWidth, titleMetrics.height()),
                   Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, r.roomName);

        p.setFont(f2);
        p.setPen(secondary);
        p.drawText(QRect(capacityLeft, textTop, capacityRight - capacityLeft + 1,
                         titleMetrics.height()),
                   Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine, capacityText);

        // Draw avatars for sessions in this room
        QList<LobbySession> roomSessions;
        for (const LobbySession &ls : m_sessions) {
            if (ls.session.roomId == r.id && ls.session.status == SessionStatus::Active) {
                roomSessions.append(ls);
            }
        }

        int avatarSize = qBound(34, qRound(qMin(rect.width(), rect.height()) * 0.19), 54);
        int cols = qMax(1, (rect.width() - 16) / (avatarSize + 6));
        int i = 0;
        for (const LobbySession &ls : roomSessions) {
            int col = i % cols;
            int row = i / cols;
            int ax = rect.x() + 8 + col * (avatarSize + 6);
            int ay = rect.y() + 44 + row * (avatarSize + 6);
            QRect avRect(ax, ay, avatarSize, avatarSize);
            m_avatarRects[ls.session.id] = avRect;

            // color by status
            QColor avatarColor = m_darkTheme ? QColor("#b77a38") : QColor("#70401f");
            p.setBrush(avatarColor);
            p.setPen(QPen(Qt::white, 2));
            p.drawEllipse(avRect);

            // initials
            QString initials;
            QStringList parts = ls.clientName.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 2) initials = QString("%1%2").arg(parts[0][0]).arg(parts[1][0]).toUpper();
            else if (!parts.isEmpty()) initials = parts[0].left(2).toUpper();
            else initials = "?";

            p.setPen(Qt::white);
            QFont af = p.font();
            af.setBold(true);
            af.setPointSize(10);
            p.setFont(af);
            p.drawText(avRect, Qt::AlignCenter, initials);

            // name tooltip-ish below
            p.setPen(title);
            QFont nf = p.font();
            nf.setBold(false);
            nf.setPointSize(8);
            p.setFont(nf);
            QRect nameRect(ax - 4, ay + avatarSize + 2, avatarSize + 8, 12);
            p.drawText(nameRect, Qt::AlignCenter | Qt::TextSingleLine, ls.clientName.split(' ').first());

            i++;
        }
    }
}

void LobbyView::mousePressEvent(QMouseEvent *e) {
    // Check avatars first
    for (auto it = m_avatarRects.begin(); it != m_avatarRects.end(); ++it) {
        if (it.value().contains(e->pos())) {
            emit sessionClicked(it.key());
            return;
        }
    }
    for (auto it = m_roomRects.begin(); it != m_roomRects.end(); ++it) {
        if (it.value().contains(e->pos())) {
            emit roomClicked(it.key());
            return;
        }
    }
    QWidget::mousePressEvent(e);
}
