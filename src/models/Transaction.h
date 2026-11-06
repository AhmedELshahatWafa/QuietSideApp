#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <QString>
#include <QDateTime>

enum class TransactionType {
    Payment,
    Charge,
    Refund,
    PackageRenewal
};

struct Transaction {
    qint64 id = 0;
    qint64 clientId = 0;
    QString clientName;
    qint64 sessionId = 0; // optional link to session
    double amount = 0.0;
    QString description;
    QString paymentMethod;
    TransactionType type = TransactionType::Charge;
    QDateTime timestamp;

    bool isValid() const { return id > 0 || clientId > 0; }
};

#endif // TRANSACTION_H
