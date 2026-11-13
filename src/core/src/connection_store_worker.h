#pragma once

#include <quarry/connection_store.h>

#include <QObject>

struct sqlite3;

namespace quarry {

// Private to ConnectionStore. Lives on the store thread and owns the sqlite3 handle.
class ConnectionStoreWorker : public QObject
{
    Q_OBJECT

public:
    explicit ConnectionStoreWorker(QString databasePath);
    ~ConnectionStoreWorker() override;

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
    bool ensureOpen(Error &error);
    bool migrate(Error &error);
    Error sqliteError(int rc, const QString &context) const;
    bool exec(const char *sql, Error &error);
    bool selectOne(const QUuid &id, ConnectionProfile &out, Error &error);

    QString m_path;
    sqlite3 *m_db = nullptr;
};

} // namespace quarry
