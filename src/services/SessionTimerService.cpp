#include "SessionTimerService.h"
#include "database/DatabaseManager.h"
#include "models/Transaction.h"

#include <QDateTime>
#include <QDebug>
#include <QStringList>

namespace {

int accrueSessionTime(Session &session, const QDateTime &now) {
    if (session.status != SessionStatus::Active || session.isPaused || session.limitReached) {
        return 0;
    }

    QDateTime checkpoint = session.lastUpdatedAt.isValid()
        ? session.lastUpdatedAt
        : session.checkInTime;
    if (!checkpoint.isValid()) {
        session.lastUpdatedAt = now;
        return 0;
    }

    qint64 elapsedSeconds = checkpoint.secsTo(now);
    if (elapsedSeconds < 0) {
        session.lastUpdatedAt = now;
        return 0;
    }

    if (elapsedSeconds == 0) return 0;
    if (session.billingMode == SessionBillingMode::Reserved) {
        const qint64 reservedSeconds = static_cast<qint64>(session.reservedDurationMinutes) * 60;
        const qint64 elapsedSoFar = static_cast<qint64>(session.billableMinutes) * 60
            + session.billableSecondsRemainder;
        const qint64 remainingSeconds = qMax<qint64>(0, reservedSeconds - elapsedSoFar);
        elapsedSeconds = qMin(elapsedSeconds, remainingSeconds);
        if (elapsedSeconds == 0) {
            session.limitReached = true;
            session.isPaused = true;
            session.status = SessionStatus::Paused;
            session.pausedSince = checkpoint;
            return 0;
        }
    }

    const int accumulatedSeconds = session.billableSecondsRemainder + static_cast<int>(elapsedSeconds);
    session.billableMinutes += accumulatedSeconds / 60;
    session.billableSecondsRemainder = accumulatedSeconds % 60;
    session.lastUpdatedAt = checkpoint.addSecs(elapsedSeconds);
    if (session.billingMode == SessionBillingMode::Reserved
        && session.billableMinutes * 60LL + session.billableSecondsRemainder
            >= static_cast<qint64>(session.reservedDurationMinutes) * 60) {
        session.limitReached = true;
        session.isPaused = true;
        session.status = SessionStatus::Paused;
        session.pausedSince = session.lastUpdatedAt;
    }
    return static_cast<int>(elapsedSeconds);
}

}

SessionTimerService::SessionTimerService(QObject *parent)
    : QObject(parent), m_db(DatabaseManager::instance()) {}

SessionTimerService::~SessionTimerService() {
    stop();
}

void SessionTimerService::start(int intervalMs) {
    if (m_running) return;
    m_timer = new QTimer(this);
    m_timer->setInterval(intervalMs);
    connect(m_timer, &QTimer::timeout, this, &SessionTimerService::onTick);
    onTick();
    m_timer->start();
    m_running = true;
}

void SessionTimerService::stop() {
    if (!m_running) return;
    m_running = false;
    if (m_timer) {
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }
}


bool SessionTimerService::checkIn(qint64 clientId, qint64 roomId, SessionBillingMode billingMode,
                                  int reservedMinutes, double reservedPrice, Session &outSession) {
    Client c = m_db.getClient(clientId);
    if (c.id == 0) {
        emit error("Client not found.");
        return false;
    }
    if (billingMode == SessionBillingMode::Reserved
        && (reservedMinutes <= 0 || reservedPrice <= 0.0)) {
        emit error("A reservation must have a valid duration and package price.");
        return false;
    }

    Session s;
    s.clientId = clientId;
    s.roomId = roomId;
    s.checkInTime = QDateTime::currentDateTime();
    s.lastUpdatedAt = s.checkInTime;
    s.billingMode = billingMode;
    s.reservedDurationMinutes = billingMode == SessionBillingMode::Reserved ? reservedMinutes : 0;
    s.reservedPrice = billingMode == SessionBillingMode::Reserved ? reservedPrice : 0.0;
    s.timeRemainingMinutes = 0;
    s.billableMinutes = 0;
    s.billableSecondsRemainder = 0;
    s.limitReached = false;
    s.isPaused = false;
    s.totalPausedMinutes = 0;
    s.status = SessionStatus::Active;

    if (!m_db.addSession(s)) {
        emit error(m_db.lastError());
        return false;
    }
    outSession = s;

    refreshActive();
    emit sessionsUpdated();
    return true;
}

bool SessionTimerService::checkoutQuote(qint64 sessionId, int &billableMinutes, double &amount) const {
    Session s = m_db.getSession(sessionId);
    if (s.id == 0 || (s.status != SessionStatus::Active && s.status != SessionStatus::Paused)) {
        return false;
    }
    accrueSessionTime(s, QDateTime::currentDateTime());
    billableMinutes = s.billableStartedMinutes();
    amount = s.billingMode == SessionBillingMode::Reserved
        ? s.reservedPrice
        : qMax(30.0, billableMinutes * 0.50);
    return true;
}

bool SessionTimerService::checkOut(qint64 sessionId, bool paymentReceived,
                                   const QString &paymentMethod, Transaction &settlement,
                                   int &freeTimeCreditedMinutes) {
    freeTimeCreditedMinutes = 0;
    Session s = m_db.getSession(sessionId);
    if (s.id == 0) {
        emit error("Session not found.");
        return false;
    }
    if ((s.status != SessionStatus::Active && s.status != SessionStatus::Paused)
        || (paymentReceived && paymentMethod.trimmed().isEmpty())) {
        emit error("The session is not active or the payment method is missing.");
        return false;
    }
    accrueSessionTime(s, QDateTime::currentDateTime());
    s.status = SessionStatus::Completed;
    s.isPaused = false;
    s.pausedSince = QDateTime();
    const int billableStartedMinutes = s.billableStartedMinutes();
    settlement.amount = s.billingMode == SessionBillingMode::Reserved
        ? s.reservedPrice
        : qMax(30.0, billableStartedMinutes * 0.50);
    settlement.description = s.billingMode == SessionBillingMode::Reserved
        ? QString("Reserved workspace package: %1 EGP for %2 hour(s)")
              .arg(s.reservedPrice, 0, 'f', 2)
              .arg(s.reservedDurationMinutes / 60.0, 0, 'f', 2)
        : QString("Workspace visit: %1 billable minute(s) at 0.50 EGP/min (30 EGP minimum)")
              .arg(billableStartedMinutes);
    settlement.paymentMethod = paymentMethod;
    settlement.timestamp = QDateTime::currentDateTime();
    if (!m_db.completeSessionWithSettlement(
            s, paymentReceived, settlement, freeTimeCreditedMinutes)) {
        emit error(m_db.lastError());
        return false;
    }
    refreshActive();
    emit sessionsUpdated();
    return true;
}

bool SessionTimerService::moveClient(qint64 sessionId, qint64 newRoomId) {
    Session s = m_db.getSession(sessionId);
    if (s.id == 0) {
        emit error("Session not found.");
        return false;
    }
    s.roomId = newRoomId;
    if (!m_db.updateSession(s)) {
        emit error(m_db.lastError());
        return false;
    }
    refreshActive();
    emit sessionsUpdated();
    return true;
}

bool SessionTimerService::upgradeReservation(qint64 sessionId, int newReservedMinutes,
                                             double newPrice) {
    Session s = m_db.getSession(sessionId);
    if (s.id == 0 || s.billingMode != SessionBillingMode::Reserved
        || !s.limitReached || newReservedMinutes <= s.reservedDurationMinutes || newPrice <= 0.0) {
        emit error("Only an expired reservation can be upgraded to a longer package.");
        return false;
    }
    if (s.status != SessionStatus::Paused) {
        emit error("Session not found.");
        return false;
    }
    const QDateTime now = QDateTime::currentDateTime();
    if (s.pausedSince.isValid()) {
        const qint64 pausedSeconds = s.pausedSince.secsTo(now);
        s.totalPausedMinutes += static_cast<int>(qMax<qint64>(0, pausedSeconds) / 60);
    }
    s.reservedDurationMinutes = newReservedMinutes;
    s.reservedPrice = newPrice;
    s.limitReached = false;
    s.isPaused = false;
    s.status = SessionStatus::Active;
    s.pausedSince = QDateTime();
    s.lastUpdatedAt = now;
    if (!m_db.updateSession(s)) {
        emit error(m_db.lastError());
        return false;
    }
    refreshActive();
    emit sessionsUpdated();
    return true;
}

bool SessionTimerService::pauseSession(qint64 sessionId) {
    Session s = m_db.getSession(sessionId);
    if (s.id == 0) {
        emit error("Session not found.");
        return false;
    }
    if (s.status != SessionStatus::Active || s.isPaused || s.limitReached) {
        return false;
    }
    const QDateTime now = QDateTime::currentDateTime();
    accrueSessionTime(s, now);
    const bool reachedReservationLimit = s.limitReached;
    if (!s.limitReached) {
        s.lastUpdatedAt = now;
        s.isPaused = true;
        s.pausedSince = now;
        s.status = SessionStatus::Paused;
    }
    if (!m_db.updateSession(s)) {
        emit error(m_db.lastError());
        return false;
    }
    refreshActive();
    emit sessionsUpdated();
    if (reachedReservationLimit) {
        const Client client = m_db.getClient(s.clientId);
        const Room room = m_db.getRoom(s.roomId);
        emit reservationTimeExpired(s, client.name, room.roomName);
    }
    return true;
}

bool SessionTimerService::resumeSession(qint64 sessionId) {
    Session s = m_db.getSession(sessionId);
    if (s.id == 0) {
        emit error("Session not found.");
        return false;
    }
    if (s.status != SessionStatus::Paused || !s.isPaused || s.limitReached) {
        return false;
    }
    s.isPaused = false;
    s.status = SessionStatus::Active;
    s.lastUpdatedAt = QDateTime::currentDateTime();
    if (s.pausedSince.isValid()) {
        int pausedMin = static_cast<int>(s.pausedSince.secsTo(QDateTime::currentDateTime())) / 60;
        s.totalPausedMinutes += pausedMin > 0 ? pausedMin : 0;
        s.pausedSince = QDateTime();
    }
    if (!m_db.updateSession(s)) {
        emit error(m_db.lastError());
        return false;
    }
    refreshActive();
    emit sessionsUpdated();
    return true;
}

QList<ActiveSessionInfo> SessionTimerService::activeSessions() const {
    QMutexLocker locker(&m_mutex);
    return m_active;
}

QList<Session> SessionTimerService::sessionsForRoom(qint64 roomId) const {
    QMutexLocker locker(&m_mutex);
    QList<Session> res;
    for (const ActiveSessionInfo &info : m_active) {
        if (info.session.roomId == roomId) res.append(info.session);
    }
    return res;
}

void SessionTimerService::refreshActive() {
    QList<Session> sessions = m_db.getActiveSessions();
    QMutexLocker locker(&m_mutex);

    m_active.clear();
    for (const Session &s : sessions) {
        ActiveSessionInfo info;
        info.session = s;
        Client c = m_db.getClient(s.clientId);
        info.clientName = c.name.isEmpty() ? QString("Client #%1").arg(s.clientId) : c.name;
        Room r = m_db.getRoom(s.roomId);
        info.roomName = r.roomName.isEmpty() ? QString("Room #%1").arg(s.roomId) : r.roomName;
        m_active.append(info);
    }
}

void SessionTimerService::onTick() {
    refreshActive();
    const QDateTime now = QDateTime::currentDateTime();
    QStringList persistenceErrors;
    QList<ActiveSessionInfo> expiredReservations;

    {
        QMutexLocker locker(&m_mutex);
        for (ActiveSessionInfo &info : m_active) {
            if (info.session.status != SessionStatus::Active || info.session.isPaused) {
                continue;
            }

            const bool wasLimitReached = info.session.limitReached;
            const int elapsedSeconds = accrueSessionTime(info.session, now);
            bool persisted = true;
            if (elapsedSeconds > 0) {
                if (!m_db.updateSession(info.session)) {
                    persistenceErrors.append(m_db.lastError());
                    persisted = false;
                }
            }
            if (!wasLimitReached && info.session.limitReached && persisted) {
                if (elapsedSeconds == 0 && !m_db.updateSession(info.session)) {
                    persistenceErrors.append(m_db.lastError());
                    continue;
                }
                expiredReservations.append(info);
            }
        }
    }

    for (const QString &message : persistenceErrors) {
        emit error(message);
    }
    for (const ActiveSessionInfo &info : expiredReservations) {
        emit reservationTimeExpired(info.session, info.clientName, info.roomName);
    }
    emit sessionsUpdated();
}
