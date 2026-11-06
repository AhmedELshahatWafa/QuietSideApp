#include "ClientManager.h"
#include "database/DatabaseManager.h"

ClientManager::ClientManager(QObject *parent)
    : QObject(parent), m_db(DatabaseManager::instance()) {}

bool ClientManager::addClient(Client &client) {
    if (client.name.trimmed().isEmpty()) {
        emit error("Client name is required.");
        return false;
    }
    bool ok = m_db.addClient(client);
    if (ok) emit clientsChanged();
    else emit error(m_db.lastError());
    return ok;
}

bool ClientManager::updateClient(const Client &client) {
    if (client.name.trimmed().isEmpty()) {
        emit error("Client name is required.");
        return false;
    }
    bool ok = m_db.updateClient(client);
    if (ok) emit clientsChanged();
    else emit error(m_db.lastError());
    return ok;
}

bool ClientManager::addFreeTime(qint64 clientId, int minutes) {
    if (minutes <= 0) {
        emit error("Free time must be greater than zero.");
        return false;
    }
    const bool ok = m_db.addFreeTimeToClient(clientId, minutes);
    if (ok) emit clientsChanged();
    else emit error(m_db.lastError());
    return ok;
}

bool ClientManager::deleteClient(qint64 id) {
    bool ok = m_db.deleteClient(id);
    if (ok) emit clientsChanged();
    else emit error(m_db.lastError());
    return ok;
}

Client ClientManager::getClient(qint64 id) {
    return m_db.getClient(id);
}

QList<Client> ClientManager::getAllClients() {
    return m_db.getAllClients();
}

QList<Client> ClientManager::searchClients(const QString &query) {
    if (query.trimmed().isEmpty()) return m_db.getAllClients();
    return m_db.searchClients(query);
}
