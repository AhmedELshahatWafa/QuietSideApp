#ifndef SESSION_H
#define SESSION_H

#include <QString>
#include <QDateTime>

enum class SessionStatus {
    Active,
    Expired,
    Completed,
    Cancelled,
    Paused
};

enum class SessionBillingMode {
    PayAsYouGo,
    Reserved
};

inline QString sessionBillingModeToString(SessionBillingMode mode) {
    return mode == SessionBillingMode::Reserved ? "reserved" : "payg";
}

inline SessionBillingMode sessionBillingModeFromString(const QString &mode) {
    return mode == "reserved" ? SessionBillingMode::Reserved : SessionBillingMode::PayAsYouGo;
}

inline QString sessionStatusToString(SessionStatus s) {
    switch (s) {
        case SessionStatus::Active: return "active";
        case SessionStatus::Expired: return "expired";
        case SessionStatus::Completed: return "completed";
        case SessionStatus::Cancelled: return "cancelled";
        case SessionStatus::Paused: return "paused";
    }
    return "active";
}

inline SessionStatus sessionStatusFromString(const QString &s) {
    if (s == "expired") return SessionStatus::Expired;
    if (s == "completed") return SessionStatus::Completed;
    if (s == "cancelled") return SessionStatus::Cancelled;
    if (s == "paused") return SessionStatus::Paused;
    return SessionStatus::Active;
}

struct Session {
    qint64 id = 0;
    qint64 clientId = 0;
    qint64 roomId = 0;
    QDateTime checkInTime;
    QDateTime lastUpdatedAt;
    int reservedDurationMinutes = 60;          // legacy per-session duration
    SessionBillingMode billingMode = SessionBillingMode::PayAsYouGo;
    double reservedPrice = 0.0;
    int timeRemainingMinutes = 0;              // actual remaining time from client's bank
    int billableMinutes = 0;
    int billableSecondsRemainder = 0;
    bool limitReached = false;
    bool isPaused = false;                     // pause state
    QDateTime pausedSince;                     // when pause started
    int totalPausedMinutes = 0;                // accumulated pause time
    SessionStatus status = SessionStatus::Active;

    int remainingSeconds(const QDateTime &now = QDateTime::currentDateTime()) const {
        if (billingMode != SessionBillingMode::Reserved) return 0;
        const qint64 elapsed = static_cast<qint64>(billableMinutes) * 60 + billableSecondsRemainder;
        return static_cast<int>(qMax<qint64>(0, static_cast<qint64>(reservedDurationMinutes) * 60 - elapsed));
    }
    int billableStartedMinutes() const {
        return billableMinutes + (billableSecondsRemainder > 0 ? 1 : 0);
    }
    bool isExpired(const QDateTime &now = QDateTime::currentDateTime()) const {
        return status == SessionStatus::Expired;
    }
    QString remainingTimeString() const {
        const int minutes = (remainingSeconds() + 59) / 60;
        return QString("%1h %2m left").arg(minutes / 60).arg(minutes % 60, 2, 10, QChar('0'));
    }
};

#endif // SESSION_H
