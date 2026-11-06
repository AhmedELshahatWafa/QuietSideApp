#ifndef CLIENT_H
#define CLIENT_H

#include <QString>
#include <QDateTime>
#include <QUuid>

struct Client {
    qint64 id = 0;
    QString name;
    QString mobile;
    QString email;
    QString membershipType;
    QString notes;
    int timeBankMinutes = 0;
    QDateTime createdAt;

    QString timeBankString() const {
        const qint64 minutes = timeBankMinutes;
        const qint64 absoluteMinutes = minutes < 0 ? -minutes : minutes;
        const QString sign = minutes < 0 ? "-" : "";
        return QString("%1%2h %3m").arg(sign).arg(absoluteMinutes / 60).arg(absoluteMinutes % 60, 2, 10, QChar('0'));
    }

    bool isValid() const { return id > 0 || !name.isEmpty(); }
};

#endif // CLIENT_H
