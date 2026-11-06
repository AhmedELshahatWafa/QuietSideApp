#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QString>
#include <QVariant>
#include <QList>
#include <QMutex>
#include <QMutex>
#include <QPair>

#include "models/Client.h"
#include "models/Room.h"
#include "models/Session.h"
#include "models/Transaction.h"

class DatabaseManager : public QObject {
    Q_OBJECT
public:
    static DatabaseManager &instance();

    bool initialize(const QString &dbPath);
    void close();

    QSqlDatabase database() const { return m_db; }
    bool isOpen() const { return m_db.isOpen(); }
    QString lastError() const { return m_lastError; }

    // Clients
    bool addClient(Client &client);
    bool updateClient(const Client &client);
    bool addFreeTimeToClient(qint64 clientId, int minutes);
    bool deleteClient(qint64 id);
    Client getClient(qint64 id);
    QList<Client> getAllClients();
    QList<Client> searchClients(const QString &query);

    // Rooms
    bool addRoom(Room &room);
    bool updateRoom(const Room &room);
    bool deleteRoom(qint64 id);
    Room getRoom(qint64 id);
    QList<Room> getAllRooms();
    void seedDefaultRooms();

    // Sessions
    bool addSession(Session &session);
    bool updateSession(const Session &session);
    bool completeSessionWithSettlement(const Session &session, bool paymentReceived,
                                       Transaction &payment, int &freeTimeCreditedMinutes);
    bool deleteSession(qint64 id);
    Session getSession(qint64 id);
    QList<Session> getActiveSessions();
    QList<Session> getSessionsByClient(qint64 clientId);
    QList<Session> getAllSessions();

    // Transactions (foundation)
    bool addTransaction(Transaction &tx);
    bool getClientOutstandingDebt(qint64 clientId, double &amount);
    bool recordClientDebtPayment(qint64 clientId, double expectedAmount,
                                 const QString &paymentMethod, Transaction &payment);
    QList<Transaction> getTransactionsByClient(qint64 clientId);
    QList<Transaction> getAllTransactions();

    // Alerts
    bool addActiveAlert(const QString &text, bool critical);
    bool loadActiveAlerts(QList<QPair<QString, bool>> &alerts);

signals:
    void databaseError(const QString &error);

private:
    explicit DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager();
    DatabaseManager(const DatabaseManager &) = delete;
    DatabaseManager &operator=(const DatabaseManager &) = delete;

    bool createTables();
    bool executeQuery(QSqlQuery &query, const QString &context);

    QSqlDatabase m_db;
    QString m_lastError;
    mutable QMutex m_mutex;
};

#endif // DATABASEMANAGER_H
