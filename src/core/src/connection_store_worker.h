#pragma once

#include "app_database.h"

#include <quarry/connection_store.h>

#include <QObject>

namespace quarry {

// Private to ConnectionStore. Lives on the store thread and owns the sqlite3 handle.
class ConnectionStoreWorker : public QObject
{
    Q_OBJECT

public:
    explicit ConnectionStoreWorker(QString databasePath);

    void load();
    void save(const ConnectionProfile &profile, const QStringList &secretFieldKeys);
    void remove(const QUuid &id);
    void touch(const QUuid &id);
    void closeDatabase();

signals:
    void loaded(QList<quarry::ConnectionProfile> profiles);
    void saved(quarry::ConnectionProfile profile);
    void removed(QUuid id);
    void failed(quarry::Error error);

private:
    bool selectOne(const QUuid &id, ConnectionProfile &out, Error &error);

    AppDatabase m_database;
};

} // namespace quarry
