#ifndef LOBBYVIEW_H
#define LOBBYVIEW_H

#include <QWidget>
#include <QList>
#include <QMap>

#include "models/Room.h"
#include "models/Session.h"

struct LobbySession {
    Session session;
    QString clientName;
};

class LobbyView : public QWidget {
    Q_OBJECT
public:
    explicit LobbyView(QWidget *parent = nullptr);

    void setRooms(const QList<Room> &rooms);
    void setSessions(const QList<LobbySession> &sessions);
    void setDarkTheme(bool darkTheme);

    QSize sizeHint() const override { return QSize(820, 440); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

signals:
    void roomClicked(qint64 roomId);
    void sessionClicked(qint64 sessionId);

private:
    QList<Room> m_rooms;
    QList<LobbySession> m_sessions;
    bool m_darkTheme = true;
    QMap<qint64, QRect> m_roomRects;
    QMap<qint64, QRect> m_avatarRects; // sessionId -> rect
};

#endif // LOBBYVIEW_H
