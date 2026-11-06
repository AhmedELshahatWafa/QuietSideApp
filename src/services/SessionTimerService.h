#ifndef SESSIONTIMERSERVICE_H
#define SESSIONTIMERSERVICE_H

#include <QObject>
#include <QTimer>
#include <QList>
#include <QMutex>
#include <QMutexLocker>

#include "models/Session.h"
#include "models/Client.h"
#include "models/Room.h"
#include "models/Transaction.h"

class DatabaseManager;

struct ActiveSessionInfo {
    Session session;
    QString clientName;
    QString roomName;
};

class SessionTimerService : public QObject {
    Q_OBJECT
public:
    explicit SessionTimerService(QObject *parent = nullptr);
    ~SessionTimerService();

    void start(int intervalMs = 5000);
    void stop();

    // Session lifecycle (called from the UI thread)
    bool checkIn(qint64 clientId, qint64 roomId, SessionBillingMode billingMode,
                 int reservedMinutes, double reservedPrice, Session &outSession);
    bool checkoutQuote(qint64 sessionId, int &billableMinutes, double &amount) const;
    bool checkOut(qint64 sessionId, bool paymentReceived, const QString &paymentMethod,
                  Transaction &settlement, int &freeTimeCreditedMinutes);
    bool moveClient(qint64 sessionId, qint64 newRoomId);
    bool upgradeReservation(qint64 sessionId, int newReservedMinutes, double newPrice);
    bool pauseSession(qint64 sessionId);
    bool resumeSession(qint64 sessionId);

    QList<ActiveSessionInfo> activeSessions() const;
    QList<Session> sessionsForRoom(qint64 roomId) const;

signals:
    void sessionsUpdated();
    void error(const QString &message);
    void reservationTimeExpired(const Session &session, const QString &clientName,
                                const QString &roomName);

private slots:
    void onTick();

private:
    void refreshActive();

    QTimer *m_timer = nullptr;
    mutable QMutex m_mutex;
    QList<ActiveSessionInfo> m_active;
    DatabaseManager &m_db;
    bool m_running = false;
};

#endif // SESSIONTIMERSERVICE_H
