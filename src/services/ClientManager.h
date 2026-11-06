#ifndef CLIENTMANAGER_H
#define CLIENTMANAGER_H

#include <QObject>
#include <QString>
#include <QList>

#include "models/Client.h"

class DatabaseManager;

class ClientManager : public QObject {
    Q_OBJECT
public:
    explicit ClientManager(QObject *parent = nullptr);

    bool addClient(Client &client);
    bool updateClient(const Client &client);
    bool addFreeTime(qint64 clientId, int minutes);
    bool deleteClient(qint64 id);
    Client getClient(qint64 id);
    QList<Client> getAllClients();
    QList<Client> searchClients(const QString &query);

signals:
    void clientsChanged();
    void error(const QString &message);

private:
    DatabaseManager &m_db;
};

#endif // CLIENTMANAGER_H
