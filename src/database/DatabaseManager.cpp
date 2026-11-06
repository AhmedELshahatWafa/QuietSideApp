#include "DatabaseManager.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

DatabaseManager &DatabaseManager::instance() {
    static DatabaseManager inst;
    return inst;
}

DatabaseManager::DatabaseManager(QObject *parent) : QObject(parent) {}

DatabaseManager::~DatabaseManager() {
    close();
}

bool DatabaseManager::initialize(const QString &dbPath) {
    QMutexLocker locker(&m_mutex);

    if (m_db.isOpen()) {
        return true;
    }

    QFileInfo fi(dbPath);
    QDir().mkpath(fi.absolutePath());

    m_db = QSqlDatabase::addDatabase("QSQLITE", "coworking_connection");
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        m_lastError = m_db.lastError().text();
        emit databaseError(m_lastError);
        return false;
    }

    QSqlQuery fk(m_db);
    fk.exec("PRAGMA foreign_keys = ON;");

    if (!createTables()) {
        m_lastError = "Failed to create tables: " + m_db.lastError().text();
        emit databaseError(m_lastError);
        return false;
    }

    seedDefaultRooms();
    return true;
}

void DatabaseManager::close() {
    QMutexLocker locker(&m_mutex);
    if (m_db.isOpen()) {
        m_db.close();
    }
    QSqlDatabase::removeDatabase("coworking_connection");
}


bool DatabaseManager::createTables() {
    QSqlQuery q(m_db);

    const QStringList statements = {
        "CREATE TABLE IF NOT EXISTS clients ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  mobile TEXT,"
        "  email TEXT,"
        "  membership_type TEXT,"
        "  notes TEXT,"
        "  time_bank_hours INTEGER DEFAULT 0,"
        "  time_bank_minutes INTEGER,"
        "  created_at TEXT NOT NULL"
        ");",

        "CREATE TABLE IF NOT EXISTS rooms ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  room_name TEXT NOT NULL,"
        "  capacity INTEGER DEFAULT 0,"
        "  description TEXT,"
        "  pos_x INTEGER DEFAULT 0,"
        "  pos_y INTEGER DEFAULT 0,"
        "  width INTEGER DEFAULT 200,"
        "  height INTEGER DEFAULT 150"
        ");",

        "CREATE TABLE IF NOT EXISTS sessions ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  client_id INTEGER NOT NULL,"
        "  room_id INTEGER NOT NULL,"
        "  check_in_time TEXT NOT NULL,"
        "  reserved_duration_minutes INTEGER NOT NULL DEFAULT 60,"
        "  billing_mode TEXT NOT NULL DEFAULT 'payg',"
        "  reserved_price REAL NOT NULL DEFAULT 0,"
        "  time_remaining_minutes INTEGER DEFAULT 0,"
        "  is_paused INTEGER DEFAULT 0,"
        "  paused_since TEXT,"
        "  total_paused_minutes INTEGER DEFAULT 0,"
        "  last_updated_at TEXT,"
        "  billable_minutes INTEGER NOT NULL DEFAULT 0,"
        "  billable_seconds_remainder INTEGER NOT NULL DEFAULT 0,"
        "  limit_reached INTEGER NOT NULL DEFAULT 0,"
        "  status TEXT NOT NULL DEFAULT 'active',"
        "  FOREIGN KEY (client_id) REFERENCES clients(id) ON DELETE CASCADE,"
        "  FOREIGN KEY (room_id) REFERENCES rooms(id) ON DELETE CASCADE"
        ");",

        "CREATE TABLE IF NOT EXISTS transactions ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  client_id INTEGER NOT NULL,"
        "  session_id INTEGER,"
        "  amount REAL NOT NULL DEFAULT 0.0,"
        "  description TEXT,"
        "  type TEXT NOT NULL DEFAULT 'charge',"
        "  payment_method TEXT,"
        "  timestamp TEXT NOT NULL,"
        "  FOREIGN KEY (client_id) REFERENCES clients(id) ON DELETE CASCADE,"
        "  FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE SET NULL"
        ");",

        "CREATE TABLE IF NOT EXISTS invoices ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  client_id INTEGER NOT NULL,"
        "  session_id INTEGER,"
        "  total_amount REAL NOT NULL DEFAULT 0.0,"
        "  status TEXT NOT NULL DEFAULT 'pending',"
        "  created_at TEXT NOT NULL,"
        "  paid_at TEXT,"
        "  FOREIGN KEY (client_id) REFERENCES clients(id) ON DELETE CASCADE"
        ");",

        "CREATE TABLE IF NOT EXISTS active_alerts ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  text TEXT NOT NULL,"
        "  critical INTEGER NOT NULL DEFAULT 0,"
        "  created_at TEXT NOT NULL"
        ");",

        "CREATE TABLE IF NOT EXISTS packages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  description TEXT,"
        "  price REAL NOT NULL DEFAULT 0.0,"
        "  duration_days INTEGER NOT NULL DEFAULT 30,"
        "  hours_included INTEGER NOT NULL DEFAULT 0"
        ");",

        "CREATE TABLE IF NOT EXISTS client_packages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  client_id INTEGER NOT NULL,"
        "  package_id INTEGER NOT NULL,"
        "  start_date TEXT NOT NULL,"
        "  end_date TEXT NOT NULL,"
        "  remaining_hours INTEGER NOT NULL DEFAULT 0,"
        "  FOREIGN KEY (client_id) REFERENCES clients(id) ON DELETE CASCADE,"
        "  FOREIGN KEY (package_id) REFERENCES packages(id) ON DELETE CASCADE"
        ");"
    };

    for (const QString &stmt : statements) {
        if (!q.exec(stmt)) {
            m_lastError = q.lastError().text();
            qWarning() << "SQL error:" << m_lastError << stmt;
            return false;
        }
    }

    // Migration: add new columns to existing tables if missing
    const QStringList alterStatements = {
        "ALTER TABLE clients ADD COLUMN time_bank_hours INTEGER DEFAULT 0",
        "ALTER TABLE clients ADD COLUMN time_bank_minutes INTEGER",
        "ALTER TABLE sessions ADD COLUMN time_remaining_minutes INTEGER DEFAULT 0",
        "ALTER TABLE sessions ADD COLUMN billing_mode TEXT NOT NULL DEFAULT 'payg'",
        "ALTER TABLE sessions ADD COLUMN reserved_price REAL NOT NULL DEFAULT 0",
        "ALTER TABLE sessions ADD COLUMN is_paused INTEGER DEFAULT 0",
        "ALTER TABLE sessions ADD COLUMN paused_since TEXT",
        "ALTER TABLE sessions ADD COLUMN total_paused_minutes INTEGER DEFAULT 0",
        "ALTER TABLE sessions ADD COLUMN last_updated_at TEXT",
        "ALTER TABLE sessions ADD COLUMN billable_minutes INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE sessions ADD COLUMN billable_seconds_remainder INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE sessions ADD COLUMN limit_reached INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE transactions ADD COLUMN payment_method TEXT",
        "ALTER TABLE invoices ADD COLUMN paid_at TEXT"
    };
    for (const QString &stmt : alterStatements) {
        q.exec(stmt); // ignore errors - column may already exist
    }
    if (!q.exec("UPDATE clients SET time_bank_minutes = time_bank_hours * 60 "
                "WHERE time_bank_minutes IS NULL")) {
        m_lastError = q.lastError().text();
        return false;
    }
    if (!q.exec("UPDATE sessions SET last_updated_at = strftime('%Y-%m-%dT%H:%M:%S', 'now', 'localtime') "
                "WHERE last_updated_at IS NULL OR last_updated_at = ''")) {
        m_lastError = q.lastError().text();
        return false;
    }
    const QString elapsedBillableSeconds =
        "max(0, CAST(strftime('%s', CASE WHEN is_paused=1 AND paused_since IS NOT NULL "
        "THEN paused_since ELSE strftime('%Y-%m-%dT%H:%M:%S', 'now', 'localtime') END) "
        "- strftime('%s', check_in_time) - total_paused_minutes * 60 AS INTEGER))";
    if (!q.exec(QString("UPDATE sessions SET billable_minutes = (%1) / 60, "
                        "billable_seconds_remainder = (%1) % 60 "
                        "WHERE billable_minutes = 0 AND billable_seconds_remainder = 0 "
                        "AND status IN ('active', 'paused')").arg(elapsedBillableSeconds))) {
        m_lastError = q.lastError().text();
        return false;
    }

    return true;
}

bool DatabaseManager::executeQuery(QSqlQuery &query, const QString &context) {
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "Query failed [" << context << "]:" << m_lastError;
        emit databaseError(m_lastError);
        return false;
    }
    return true;
}

// ---------- Clients ----------

bool DatabaseManager::addClient(Client &client) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO clients (name, mobile, email, membership_type, notes, time_bank_hours, time_bank_minutes, created_at) "
              "VALUES (:name, :mobile, :email, :mt, :notes, :tbh, :tbm, :created)");
    q.bindValue(":name", client.name);
    q.bindValue(":mobile", client.mobile);
    q.bindValue(":email", client.email);
    q.bindValue(":mt", client.membershipType);
    q.bindValue(":notes", client.notes);
    q.bindValue(":tbh", client.timeBankMinutes / 60);
    q.bindValue(":tbm", client.timeBankMinutes);
    q.bindValue(":created", (client.createdAt.isValid() ? client.createdAt : QDateTime::currentDateTime()).toString(Qt::ISODate));
    if (!executeQuery(q, "addClient")) return false;
    client.id = q.lastInsertId().toLongLong();
    client.createdAt = client.createdAt.isValid() ? client.createdAt : QDateTime::currentDateTime();
    return true;
}

bool DatabaseManager::updateClient(const Client &client) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("UPDATE clients SET name=:name, mobile=:mobile, email=:email, "
              "membership_type=:mt, notes=:notes WHERE id=:id");
    q.bindValue(":name", client.name);
    q.bindValue(":mobile", client.mobile);
    q.bindValue(":email", client.email);
    q.bindValue(":mt", client.membershipType);
    q.bindValue(":notes", client.notes);
    q.bindValue(":id", client.id);
    return executeQuery(q, "updateClient");
}

bool DatabaseManager::addFreeTimeToClient(qint64 clientId, int minutes) {
    QMutexLocker locker(&m_mutex);
    if (clientId <= 0 || minutes <= 0) {
        m_lastError = "A valid client and a positive amount of free time are required.";
        return false;
    }

    QSqlQuery q(m_db);
    q.prepare("UPDATE clients SET time_bank_minutes=COALESCE(time_bank_minutes, 0) + :minutes, "
              "time_bank_hours=CAST((COALESCE(time_bank_minutes, 0) + :minutes) / 60 AS INTEGER) "
              "WHERE id=:id");
    q.bindValue(":minutes", minutes);
    q.bindValue(":id", clientId);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    if (q.numRowsAffected() != 1) {
        m_lastError = "Client not found.";
        return false;
    }
    return true;
}

bool DatabaseManager::deleteClient(qint64 id) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM clients WHERE id=:id");
    q.bindValue(":id", id);
    return executeQuery(q, "deleteClient");
}

Client DatabaseManager::getClient(qint64 id) {
    QMutexLocker locker(&m_mutex);
    Client c;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, name, mobile, email, membership_type, notes, time_bank_minutes, created_at FROM clients WHERE id=:id");
    q.bindValue(":id", id);
    if (q.exec() && q.next()) {
        c.id = q.value(0).toLongLong();
        c.name = q.value(1).toString();
        c.mobile = q.value(2).toString();
        c.email = q.value(3).toString();
        c.membershipType = q.value(4).toString();
        c.notes = q.value(5).toString();
        c.timeBankMinutes = q.value(6).toInt();
        c.createdAt = QDateTime::fromString(q.value(7).toString(), Qt::ISODate);
    }
    return c;
}

QList<Client> DatabaseManager::getAllClients() {
    QMutexLocker locker(&m_mutex);
    QList<Client> list;
    QSqlQuery q(m_db);
    q.exec("SELECT id, name, mobile, email, membership_type, notes, time_bank_minutes, created_at FROM clients ORDER BY name");
    while (q.next()) {
        Client c;
        c.id = q.value(0).toLongLong();
        c.name = q.value(1).toString();
        c.mobile = q.value(2).toString();
        c.email = q.value(3).toString();
        c.membershipType = q.value(4).toString();
        c.notes = q.value(5).toString();
        c.timeBankMinutes = q.value(6).toInt();
        c.createdAt = QDateTime::fromString(q.value(7).toString(), Qt::ISODate);
        list.append(c);
    }
    return list;
}

QList<Client> DatabaseManager::searchClients(const QString &query) {
    QMutexLocker locker(&m_mutex);
    QList<Client> list;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, name, mobile, email, membership_type, notes, time_bank_minutes, created_at "
              "FROM clients WHERE name LIKE :q OR mobile LIKE :q OR email LIKE :q ORDER BY name");
    q.bindValue(":q", "%" + query + "%");
    if (q.exec()) {
        while (q.next()) {
            Client c;
            c.id = q.value(0).toLongLong();
            c.name = q.value(1).toString();
            c.mobile = q.value(2).toString();
            c.email = q.value(3).toString();
            c.membershipType = q.value(4).toString();
            c.notes = q.value(5).toString();
            c.timeBankMinutes = q.value(6).toInt();
            c.createdAt = QDateTime::fromString(q.value(7).toString(), Qt::ISODate);
            list.append(c);
        }
    }
    return list;
}

// ---------- Rooms ----------

bool DatabaseManager::addRoom(Room &room) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO rooms (room_name, capacity, description, pos_x, pos_y, width, height) "
              "VALUES (:name, :cap, :desc, :x, :y, :w, :h)");
    q.bindValue(":name", room.roomName);
    q.bindValue(":cap", room.capacity);
    q.bindValue(":desc", room.description);
    q.bindValue(":x", room.x);
    q.bindValue(":y", room.y);
    q.bindValue(":w", room.width);
    q.bindValue(":h", room.height);
    if (!executeQuery(q, "addRoom")) return false;
    room.id = q.lastInsertId().toLongLong();
    return true;
}

bool DatabaseManager::updateRoom(const Room &room) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("UPDATE rooms SET room_name=:name, capacity=:cap, description=:desc, "
              "pos_x=:x, pos_y=:y, width=:w, height=:h WHERE id=:id");
    q.bindValue(":name", room.roomName);
    q.bindValue(":cap", room.capacity);
    q.bindValue(":desc", room.description);
    q.bindValue(":x", room.x);
    q.bindValue(":y", room.y);
    q.bindValue(":w", room.width);
    q.bindValue(":h", room.height);
    q.bindValue(":id", room.id);
    return executeQuery(q, "updateRoom");
}

bool DatabaseManager::deleteRoom(qint64 id) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM rooms WHERE id=:id");
    q.bindValue(":id", id);
    return executeQuery(q, "deleteRoom");
}

Room DatabaseManager::getRoom(qint64 id) {
    QMutexLocker locker(&m_mutex);
    Room r;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, room_name, capacity, description, pos_x, pos_y, width, height FROM rooms WHERE id=:id");
    q.bindValue(":id", id);
    if (q.exec() && q.next()) {
        r.id = q.value(0).toLongLong();
        r.roomName = q.value(1).toString();
        r.capacity = q.value(2).toInt();
        r.description = q.value(3).toString();
        r.x = q.value(4).toInt();
        r.y = q.value(5).toInt();
        r.width = q.value(6).toInt();
        r.height = q.value(7).toInt();
    }
    return r;
}

QList<Room> DatabaseManager::getAllRooms() {
    QMutexLocker locker(&m_mutex);
    QList<Room> list;
    QSqlQuery q(m_db);
    q.exec("SELECT id, room_name, capacity, description, pos_x, pos_y, width, height FROM rooms ORDER BY id");
    while (q.next()) {
        Room r;
        r.id = q.value(0).toLongLong();
        r.roomName = q.value(1).toString();
        r.capacity = q.value(2).toInt();
        r.description = q.value(3).toString();
        r.x = q.value(4).toInt();
        r.y = q.value(5).toInt();
        r.width = q.value(6).toInt();
        r.height = q.value(7).toInt();
        list.append(r);
    }
    return list;
}

void DatabaseManager::seedDefaultRooms() {
    // Keep room IDs stable because sessions reference them.
    QSqlQuery clearQ(m_db);
    if (!clearQ.exec("SELECT 1 FROM rooms LIMIT 1")) {
        m_lastError = clearQ.lastError().text();
        qWarning() << "Failed to check existing rooms:" << m_lastError;
        return;
    }
    if (clearQ.next()) {
        return;
    }

    struct R { const char *name; int cap; int x; int y; int w; int h; const char *desc; };
    R rooms[] = {
        {"Lounge", 5, 20, 20, 180, 140, "Main lounge and reception area"},
        {"Main Room", 8, 20, 180, 260, 190, "Main coworking area"},
        {"Small Room", 4, 300, 20, 140, 110, "Quiet small room"},
        {"Balcony", 8, 300, 150, 220, 90, "Small balcony nook"}
    };
    for (const R &r : rooms) {
        QSqlQuery q(m_db);
        q.prepare("INSERT INTO rooms (room_name, capacity, description, pos_x, pos_y, width, height) "
                  "VALUES (:name, :cap, :desc, :x, :y, :w, :h)");
        q.bindValue(":name", r.name);
        q.bindValue(":cap", r.cap);
        q.bindValue(":desc", r.desc);
        q.bindValue(":x", r.x);
        q.bindValue(":y", r.y);
        q.bindValue(":w", r.w);
        q.bindValue(":h", r.h);
        q.exec();
    }
}


// ---------- Sessions ----------

bool DatabaseManager::addSession(Session &session) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO sessions (client_id, room_id, check_in_time, reserved_duration_minutes, "
              "billing_mode, reserved_price, time_remaining_minutes, is_paused, paused_since, "
              "total_paused_minutes, last_updated_at, billable_minutes, billable_seconds_remainder, "
              "limit_reached, status) "
              "VALUES (:cid, :rid, :cin, :dur, :mode, :price, :trm, :paused, :psince, :tpm, "
              ":updated, :billable, :remainder, :limit, :status)");
    q.bindValue(":cid", session.clientId);
    q.bindValue(":rid", session.roomId);
    q.bindValue(":cin", session.checkInTime.toString(Qt::ISODate));
    q.bindValue(":dur", session.reservedDurationMinutes);
    q.bindValue(":mode", sessionBillingModeToString(session.billingMode));
    q.bindValue(":price", session.reservedPrice);
    q.bindValue(":trm", session.timeRemainingMinutes);
    q.bindValue(":paused", session.isPaused ? 1 : 0);
    q.bindValue(":psince", session.pausedSince.isValid() ? session.pausedSince.toString(Qt::ISODate) : QString());
    q.bindValue(":tpm", session.totalPausedMinutes);
    q.bindValue(":updated", session.lastUpdatedAt.toString(Qt::ISODate));
    q.bindValue(":billable", session.billableMinutes);
    q.bindValue(":remainder", session.billableSecondsRemainder);
    q.bindValue(":limit", session.limitReached ? 1 : 0);
    q.bindValue(":status", sessionStatusToString(session.status));
    if (!executeQuery(q, "addSession")) return false;
    session.id = q.lastInsertId().toLongLong();
    return true;
}

bool DatabaseManager::updateSession(const Session &session) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("UPDATE sessions SET client_id=:cid, room_id=:rid, check_in_time=:cin, "
              "reserved_duration_minutes=:dur, billing_mode=:mode, reserved_price=:price, time_remaining_minutes=:trm, "
              "is_paused=:paused, paused_since=:psince, total_paused_minutes=:tpm, "
              "last_updated_at=:updated, billable_minutes=:billable, billable_seconds_remainder=:remainder, "
              "limit_reached=:limit, status=:status WHERE id=:id");
    q.bindValue(":cid", session.clientId);
    q.bindValue(":rid", session.roomId);
    q.bindValue(":cin", session.checkInTime.toString(Qt::ISODate));
    q.bindValue(":dur", session.reservedDurationMinutes);
    q.bindValue(":mode", sessionBillingModeToString(session.billingMode));
    q.bindValue(":price", session.reservedPrice);
    q.bindValue(":trm", session.timeRemainingMinutes);
    q.bindValue(":paused", session.isPaused ? 1 : 0);
    q.bindValue(":psince", session.pausedSince.isValid() ? session.pausedSince.toString(Qt::ISODate) : QString());
    q.bindValue(":tpm", session.totalPausedMinutes);
    q.bindValue(":updated", session.lastUpdatedAt.toString(Qt::ISODate));
    q.bindValue(":billable", session.billableMinutes);
    q.bindValue(":remainder", session.billableSecondsRemainder);
    q.bindValue(":limit", session.limitReached ? 1 : 0);
    q.bindValue(":status", sessionStatusToString(session.status));
    q.bindValue(":id", session.id);
    return executeQuery(q, "updateSession");
}

bool DatabaseManager::completeSessionWithSettlement(const Session &session, bool paymentReceived,
                                                    Transaction &payment,
                                                    int &freeTimeCreditedMinutes) {
    QMutexLocker locker(&m_mutex);
    freeTimeCreditedMinutes = 0;
    if (session.id <= 0 || payment.amount <= 0.0
        || (paymentReceived && payment.paymentMethod.trimmed().isEmpty())) {
        m_lastError = "A valid session and positive amount are required; paid visits also need a payment method.";
        return false;
    }
    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        return false;
    }

    QSqlQuery updateSessionQuery(m_db);
    updateSessionQuery.prepare("UPDATE sessions SET client_id=:cid, room_id=:rid, check_in_time=:cin, "
                               "reserved_duration_minutes=:dur, billing_mode=:mode, reserved_price=:price, "
                               "time_remaining_minutes=:trm, "
                               "is_paused=:paused, paused_since=:psince, total_paused_minutes=:tpm, "
                               "last_updated_at=:updated, billable_minutes=:billable, "
                               "billable_seconds_remainder=:remainder, limit_reached=:limit, status='completed' "
                               "WHERE id=:id AND status IN ('active', 'paused')");
    updateSessionQuery.bindValue(":cid", session.clientId);
    updateSessionQuery.bindValue(":rid", session.roomId);
    updateSessionQuery.bindValue(":cin", session.checkInTime.toString(Qt::ISODate));
    updateSessionQuery.bindValue(":dur", session.reservedDurationMinutes);
    updateSessionQuery.bindValue(":mode", sessionBillingModeToString(session.billingMode));
    updateSessionQuery.bindValue(":price", session.reservedPrice);
    updateSessionQuery.bindValue(":trm", session.timeRemainingMinutes);
    updateSessionQuery.bindValue(":paused", session.isPaused ? 1 : 0);
    updateSessionQuery.bindValue(":psince", session.pausedSince.isValid() ? session.pausedSince.toString(Qt::ISODate) : QString());
    updateSessionQuery.bindValue(":tpm", session.totalPausedMinutes);
    updateSessionQuery.bindValue(":updated", session.lastUpdatedAt.toString(Qt::ISODate));
    updateSessionQuery.bindValue(":billable", session.billableMinutes);
    updateSessionQuery.bindValue(":remainder", session.billableSecondsRemainder);
    updateSessionQuery.bindValue(":limit", session.limitReached ? 1 : 0);
    updateSessionQuery.bindValue(":id", session.id);
    if (!updateSessionQuery.exec() || updateSessionQuery.numRowsAffected() == 0) {
        m_lastError = updateSessionQuery.lastError().text();
        if (m_lastError.isEmpty()) m_lastError = "Session not found.";
        m_db.rollback();
        return false;
    }

    if (session.billingMode == SessionBillingMode::Reserved) {
        freeTimeCreditedMinutes = qMax(
            0, session.reservedDurationMinutes - session.billableStartedMinutes());
        if (freeTimeCreditedMinutes > 0) {
            QSqlQuery creditTimeQuery(m_db);
            creditTimeQuery.prepare(
                "UPDATE clients SET "
                "time_bank_minutes=COALESCE(time_bank_minutes, 0) + :minutes, "
                "time_bank_hours=CAST((COALESCE(time_bank_minutes, 0) + :minutes) / 60 AS INTEGER) "
                "WHERE id=:id");
            creditTimeQuery.bindValue(":minutes", freeTimeCreditedMinutes);
            creditTimeQuery.bindValue(":id", session.clientId);
            if (!creditTimeQuery.exec() || creditTimeQuery.numRowsAffected() != 1) {
                m_lastError = creditTimeQuery.lastError().text();
                if (m_lastError.isEmpty()) m_lastError = "Could not credit unused reserved time.";
                m_db.rollback();
                freeTimeCreditedMinutes = 0;
                return false;
            }
            payment.description += QString("; %1 unused minute(s) credited to time bank")
                                       .arg(freeTimeCreditedMinutes);
        }
    }

    if (paymentReceived) {
        payment.clientId = session.clientId;
        payment.sessionId = session.id;
        payment.type = TransactionType::Payment;
        payment.timestamp = QDateTime::currentDateTime();
        QSqlQuery paymentQuery(m_db);
        paymentQuery.prepare("INSERT INTO transactions "
                             "(client_id, session_id, amount, description, type, payment_method, timestamp) "
                             "VALUES (:client, :session, :amount, :description, 'payment', :method, :timestamp)");
        paymentQuery.bindValue(":client", payment.clientId);
        paymentQuery.bindValue(":session", payment.sessionId);
        paymentQuery.bindValue(":amount", payment.amount);
        paymentQuery.bindValue(":description", payment.description);
        paymentQuery.bindValue(":method", payment.paymentMethod);
        paymentQuery.bindValue(":timestamp", payment.timestamp.toString(Qt::ISODate));
        if (!paymentQuery.exec()) {
            m_lastError = paymentQuery.lastError().text();
            m_db.rollback();
            return false;
        }
        payment.id = paymentQuery.lastInsertId().toLongLong();
    } else {
        QSqlQuery invoiceQuery(m_db);
        invoiceQuery.prepare("INSERT INTO invoices (client_id, session_id, total_amount, status, created_at) "
                             "VALUES (:client, :session, :amount, 'pending', :created)");
        invoiceQuery.bindValue(":client", session.clientId);
        invoiceQuery.bindValue(":session", session.id);
        invoiceQuery.bindValue(":amount", payment.amount);
        invoiceQuery.bindValue(":created", QDateTime::currentDateTime().toString(Qt::ISODate));
        if (!invoiceQuery.exec()) {
            m_lastError = invoiceQuery.lastError().text();
            m_db.rollback();
            return false;
        }
        payment.clientId = session.clientId;
        payment.sessionId = session.id;
        payment.type = TransactionType::Charge;
        payment.timestamp = QDateTime::currentDateTime();
    }

    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        m_db.rollback();
        freeTimeCreditedMinutes = 0;
        return false;
    }
    return true;
}

bool DatabaseManager::deleteSession(qint64 id) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM sessions WHERE id=:id");
    q.bindValue(":id", id);
    return executeQuery(q, "deleteSession");
}

Session DatabaseManager::getSession(qint64 id) {
    QMutexLocker locker(&m_mutex);
    Session s;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, client_id, room_id, check_in_time, reserved_duration_minutes, billing_mode, reserved_price, "
              "time_remaining_minutes, is_paused, paused_since, total_paused_minutes, status, last_updated_at, "
              "billable_minutes, billable_seconds_remainder, limit_reached "
              "FROM sessions WHERE id=:id");
    q.bindValue(":id", id);
    if (q.exec() && q.next()) {
        s.id = q.value(0).toLongLong();
        s.clientId = q.value(1).toLongLong();
        s.roomId = q.value(2).toLongLong();
        s.checkInTime = QDateTime::fromString(q.value(3).toString(), Qt::ISODate);
        s.reservedDurationMinutes = q.value(4).toInt();
        s.billingMode = sessionBillingModeFromString(q.value(5).toString());
        s.reservedPrice = q.value(6).toDouble();
        s.timeRemainingMinutes = q.value(7).toInt();
        s.isPaused = q.value(8).toInt() == 1;
        s.pausedSince = QDateTime::fromString(q.value(9).toString(), Qt::ISODate);
        s.totalPausedMinutes = q.value(10).toInt();
        s.status = sessionStatusFromString(q.value(11).toString());
        s.lastUpdatedAt = QDateTime::fromString(q.value(12).toString(), Qt::ISODate);
        s.billableMinutes = q.value(13).toInt();
        s.billableSecondsRemainder = q.value(14).toInt();
        s.limitReached = q.value(15).toInt() == 1;
    }
    return s;
}

QList<Session> DatabaseManager::getActiveSessions() {
    QMutexLocker locker(&m_mutex);
    QList<Session> list;
    QSqlQuery q(m_db);
    q.exec("SELECT id, client_id, room_id, check_in_time, reserved_duration_minutes, billing_mode, reserved_price, "
           "time_remaining_minutes, is_paused, paused_since, total_paused_minutes, status, last_updated_at, "
           "billable_minutes, billable_seconds_remainder, limit_reached "
           "FROM sessions WHERE status IN ('active','paused') ORDER BY check_in_time");
    while (q.next()) {
        Session s;
        s.id = q.value(0).toLongLong();
        s.clientId = q.value(1).toLongLong();
        s.roomId = q.value(2).toLongLong();
        s.checkInTime = QDateTime::fromString(q.value(3).toString(), Qt::ISODate);
        s.reservedDurationMinutes = q.value(4).toInt();
        s.billingMode = sessionBillingModeFromString(q.value(5).toString());
        s.reservedPrice = q.value(6).toDouble();
        s.timeRemainingMinutes = q.value(7).toInt();
        s.isPaused = q.value(8).toInt() == 1;
        s.pausedSince = QDateTime::fromString(q.value(9).toString(), Qt::ISODate);
        s.totalPausedMinutes = q.value(10).toInt();
        s.status = sessionStatusFromString(q.value(11).toString());
        s.lastUpdatedAt = QDateTime::fromString(q.value(12).toString(), Qt::ISODate);
        s.billableMinutes = q.value(13).toInt();
        s.billableSecondsRemainder = q.value(14).toInt();
        s.limitReached = q.value(15).toInt() == 1;
        list.append(s);
    }
    return list;
}

QList<Session> DatabaseManager::getSessionsByClient(qint64 clientId) {
    QMutexLocker locker(&m_mutex);
    QList<Session> list;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, client_id, room_id, check_in_time, reserved_duration_minutes, billing_mode, reserved_price, "
              "time_remaining_minutes, is_paused, paused_since, total_paused_minutes, status, last_updated_at, "
              "billable_minutes, billable_seconds_remainder, limit_reached "
              "FROM sessions WHERE client_id=:cid ORDER BY check_in_time DESC");
    q.bindValue(":cid", clientId);
    if (q.exec()) {
        while (q.next()) {
            Session s;
            s.id = q.value(0).toLongLong();
            s.clientId = q.value(1).toLongLong();
            s.roomId = q.value(2).toLongLong();
            s.checkInTime = QDateTime::fromString(q.value(3).toString(), Qt::ISODate);
            s.reservedDurationMinutes = q.value(4).toInt();
            s.billingMode = sessionBillingModeFromString(q.value(5).toString());
            s.reservedPrice = q.value(6).toDouble();
            s.timeRemainingMinutes = q.value(7).toInt();
            s.isPaused = q.value(8).toInt() == 1;
            s.pausedSince = QDateTime::fromString(q.value(9).toString(), Qt::ISODate);
            s.totalPausedMinutes = q.value(10).toInt();
            s.status = sessionStatusFromString(q.value(11).toString());
            s.lastUpdatedAt = QDateTime::fromString(q.value(12).toString(), Qt::ISODate);
            s.billableMinutes = q.value(13).toInt();
            s.billableSecondsRemainder = q.value(14).toInt();
            s.limitReached = q.value(15).toInt() == 1;
            list.append(s);
        }
    }
    return list;
}

QList<Session> DatabaseManager::getAllSessions() {
    QMutexLocker locker(&m_mutex);
    QList<Session> list;
    QSqlQuery q(m_db);
    q.exec("SELECT id, client_id, room_id, check_in_time, reserved_duration_minutes, billing_mode, reserved_price, "
           "time_remaining_minutes, is_paused, paused_since, total_paused_minutes, status, last_updated_at, "
           "billable_minutes, billable_seconds_remainder, limit_reached "
           "FROM sessions ORDER BY check_in_time DESC");
    while (q.next()) {
        Session s;
        s.id = q.value(0).toLongLong();
        s.clientId = q.value(1).toLongLong();
        s.roomId = q.value(2).toLongLong();
        s.checkInTime = QDateTime::fromString(q.value(3).toString(), Qt::ISODate);
        s.reservedDurationMinutes = q.value(4).toInt();
        s.billingMode = sessionBillingModeFromString(q.value(5).toString());
        s.reservedPrice = q.value(6).toDouble();
        s.timeRemainingMinutes = q.value(7).toInt();
        s.isPaused = q.value(8).toInt() == 1;
        s.pausedSince = QDateTime::fromString(q.value(9).toString(), Qt::ISODate);
        s.totalPausedMinutes = q.value(10).toInt();
        s.status = sessionStatusFromString(q.value(11).toString());
        s.lastUpdatedAt = QDateTime::fromString(q.value(12).toString(), Qt::ISODate);
        s.billableMinutes = q.value(13).toInt();
        s.billableSecondsRemainder = q.value(14).toInt();
        s.limitReached = q.value(15).toInt() == 1;
        list.append(s);
    }
    return list;
}


// ---------- Transactions ----------

bool DatabaseManager::addTransaction(Transaction &tx) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO transactions (client_id, session_id, amount, description, type, payment_method, timestamp) "
              "VALUES (:cid, :sid, :amt, :desc, :type, :method, :ts)");
    q.bindValue(":cid", tx.clientId);
    q.bindValue(":sid", tx.sessionId > 0 ? tx.sessionId : QVariant());
    q.bindValue(":amt", tx.amount);
    q.bindValue(":desc", tx.description);
    QString typeStr;
    switch (tx.type) {
        case TransactionType::Payment: typeStr = "payment"; break;
        case TransactionType::Charge: typeStr = "charge"; break;
        case TransactionType::Refund: typeStr = "refund"; break;
        case TransactionType::PackageRenewal: typeStr = "package_renewal"; break;
    }
    q.bindValue(":type", typeStr);
    q.bindValue(":method", tx.paymentMethod);
    q.bindValue(":ts", (tx.timestamp.isValid() ? tx.timestamp : QDateTime::currentDateTime()).toString(Qt::ISODate));
    if (!executeQuery(q, "addTransaction")) return false;
    tx.id = q.lastInsertId().toLongLong();
    return true;
}

bool DatabaseManager::getClientOutstandingDebt(qint64 clientId, double &amount) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("SELECT COALESCE((SELECT SUM(total_amount) FROM invoices "
              "WHERE client_id=:invoice_client AND status='pending'), 0) "
              "+ COALESCE((SELECT max(0, -time_bank_minutes) * 0.50 FROM clients "
              "WHERE id=:client_id), 0)");
    q.bindValue(":invoice_client", clientId);
    q.bindValue(":client_id", clientId);
    if (!q.exec() || !q.next()) {
        m_lastError = q.lastError().text();
        return false;
    }
    amount = q.value(0).toDouble();
    return true;
}

bool DatabaseManager::recordClientDebtPayment(qint64 clientId, double expectedAmount,
                                               const QString &paymentMethod, Transaction &payment) {
    QMutexLocker locker(&m_mutex);
    if (clientId <= 0 || paymentMethod.trimmed().isEmpty()) {
        m_lastError = "A valid client and payment method are required.";
        return false;
    }
    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        return false;
    }

    QSqlQuery clientQuery(m_db);
    clientQuery.prepare("SELECT time_bank_minutes FROM clients WHERE id=:id");
    clientQuery.bindValue(":id", clientId);
    if (!clientQuery.exec() || !clientQuery.next()) {
        m_lastError = clientQuery.lastError().text();
        if (m_lastError.isEmpty()) m_lastError = "Client not found.";
        m_db.rollback();
        return false;
    }
    const qint64 minutesOwed = qMax<qint64>(0, -static_cast<qint64>(clientQuery.value(0).toInt()));
    clientQuery.finish();
    QSqlQuery invoiceTotalQuery(m_db);
    invoiceTotalQuery.prepare("SELECT COALESCE(SUM(total_amount), 0) FROM invoices "
                              "WHERE client_id=:id AND status='pending'");
    invoiceTotalQuery.bindValue(":id", clientId);
    if (!invoiceTotalQuery.exec() || !invoiceTotalQuery.next()) {
        m_lastError = invoiceTotalQuery.lastError().text();
        m_db.rollback();
        return false;
    }
    const double invoiceDebt = invoiceTotalQuery.value(0).toDouble();
    invoiceTotalQuery.finish();
    const double totalDebt = invoiceDebt + minutesOwed * 0.50;
    if (totalDebt <= 0.0) {
        m_lastError = "This client has no outstanding debt.";
        m_db.rollback();
        return false;
    }
    if (qAbs(totalDebt - expectedAmount) >= 0.005) {
        m_lastError = "The outstanding amount changed. Review the updated total before confirming payment.";
        m_db.rollback();
        return false;
    }

    payment.clientId = clientId;
    payment.sessionId = 0;
    payment.amount = totalDebt;
    payment.description = "Outstanding session and legacy debt settlement";
    payment.paymentMethod = paymentMethod.trimmed();
    payment.type = TransactionType::Payment;
    payment.timestamp = QDateTime::currentDateTime();

    if (minutesOwed > 0) {
        QSqlQuery clearDebtQuery(m_db);
        clearDebtQuery.prepare("UPDATE clients SET time_bank_minutes=0, time_bank_hours=0 "
                               "WHERE id=:id AND time_bank_minutes<0");
        clearDebtQuery.bindValue(":id", clientId);
        if (!clearDebtQuery.exec() || clearDebtQuery.numRowsAffected() != 1) {
            m_lastError = clearDebtQuery.lastError().text();
            if (m_lastError.isEmpty()) m_lastError = "The client's debt changed before payment was recorded.";
            m_db.rollback();
            return false;
        }
    }

    QSqlQuery settleInvoicesQuery(m_db);
    settleInvoicesQuery.prepare("UPDATE invoices SET status='paid', paid_at=:paid_at "
                                "WHERE client_id=:id AND status='pending'");
    settleInvoicesQuery.bindValue(":paid_at", payment.timestamp.toString(Qt::ISODate));
    settleInvoicesQuery.bindValue(":id", clientId);
    if (!settleInvoicesQuery.exec()) {
        m_lastError = settleInvoicesQuery.lastError().text();
        m_db.rollback();
        return false;
    }

    QSqlQuery paymentQuery(m_db);
    paymentQuery.prepare("INSERT INTO transactions "
                         "(client_id, session_id, amount, description, type, payment_method, timestamp) "
                         "VALUES (:client, NULL, :amount, :description, 'payment', :method, :timestamp)");
    paymentQuery.bindValue(":client", payment.clientId);
    paymentQuery.bindValue(":amount", payment.amount);
    paymentQuery.bindValue(":description", payment.description);
    paymentQuery.bindValue(":method", payment.paymentMethod);
    paymentQuery.bindValue(":timestamp", payment.timestamp.toString(Qt::ISODate));
    if (!paymentQuery.exec()) {
        m_lastError = paymentQuery.lastError().text();
        m_db.rollback();
        return false;
    }
    payment.id = paymentQuery.lastInsertId().toLongLong();

    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        m_db.rollback();
        return false;
    }
    return true;
}

QList<Transaction> DatabaseManager::getTransactionsByClient(qint64 clientId) {
    QMutexLocker locker(&m_mutex);
    QList<Transaction> list;
    QSqlQuery q(m_db);
    q.prepare("SELECT t.id, t.client_id, c.name, t.session_id, t.amount, t.description, "
              "t.type, t.payment_method, t.timestamp FROM transactions t "
              "JOIN clients c ON c.id=t.client_id WHERE t.client_id=:cid ORDER BY t.timestamp DESC");
    q.bindValue(":cid", clientId);
    if (q.exec()) {
        while (q.next()) {
            Transaction t;
            t.id = q.value(0).toLongLong();
            t.clientId = q.value(1).toLongLong();
            t.clientName = q.value(2).toString();
            t.sessionId = q.value(3).toLongLong();
            t.amount = q.value(4).toDouble();
            t.description = q.value(5).toString();
            QString ts = q.value(6).toString();
            if (ts == "payment") t.type = TransactionType::Payment;
            else if (ts == "refund") t.type = TransactionType::Refund;
            else if (ts == "package_renewal") t.type = TransactionType::PackageRenewal;
            else t.type = TransactionType::Charge;
            t.paymentMethod = q.value(7).toString();
            t.timestamp = QDateTime::fromString(q.value(8).toString(), Qt::ISODate);
            list.append(t);
        }
    }
    return list;
}

QList<Transaction> DatabaseManager::getAllTransactions() {
    QMutexLocker locker(&m_mutex);
    QList<Transaction> list;
    QSqlQuery q(m_db);
    q.exec("SELECT t.id, t.client_id, c.name, t.session_id, t.amount, t.description, "
           "t.type, t.payment_method, t.timestamp FROM transactions t "
           "JOIN clients c ON c.id=t.client_id ORDER BY t.timestamp DESC");
    while (q.next()) {
        Transaction t;
        t.id = q.value(0).toLongLong();
        t.clientId = q.value(1).toLongLong();
        t.clientName = q.value(2).toString();
        t.sessionId = q.value(3).toLongLong();
        t.amount = q.value(4).toDouble();
        t.description = q.value(5).toString();
        QString ts = q.value(6).toString();
        if (ts == "payment") t.type = TransactionType::Payment;
        else if (ts == "refund") t.type = TransactionType::Refund;
        else if (ts == "package_renewal") t.type = TransactionType::PackageRenewal;
        else t.type = TransactionType::Charge;
        t.paymentMethod = q.value(7).toString();
        t.timestamp = QDateTime::fromString(q.value(8).toString(), Qt::ISODate);
        list.append(t);
    }
    return list;
}

bool DatabaseManager::addActiveAlert(const QString &text, bool critical) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO active_alerts (text, critical, created_at) "
              "VALUES (:text, :critical, :created)");
    q.bindValue(":text", text);
    q.bindValue(":critical", critical ? 1 : 0);
    q.bindValue(":created", QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::loadActiveAlerts(QList<QPair<QString, bool>> &alerts) {
    QMutexLocker locker(&m_mutex);
    alerts.clear();

    QSqlQuery q(m_db);
    if (!q.exec("SELECT text, critical FROM active_alerts ORDER BY id")) {
        m_lastError = q.lastError().text();
        return false;
    }
    while (q.next()) {
        alerts.append(qMakePair(q.value(0).toString(), q.value(1).toBool()));
    }
    return true;
}
