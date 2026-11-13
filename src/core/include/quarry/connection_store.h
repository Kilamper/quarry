#pragma once

#include <quarry/connection_profile.h>
#include <quarry/result.h>
// Declares the Q_DECLARE_METATYPE for quarry::Error used by failed().
#include <quarry/session.h>

#include <QList>
#include <QObject>
#include <QStringList>

class QThread;

namespace quarry {

class ConnectionStoreWorker;

// UI-thread facade over the `connections` table of quarry.db. The sqlite handle lives on a
// dedicated worker thread (opened lazily by the first operation); every method returns at once
// and the outcome arrives as a queued signal. Operations run in call order.
class ConnectionStore : public QObject
{
    Q_OBJECT

public:
    explicit ConnectionStore(QString databasePath, QObject *parent = nullptr);
    // Finishes the queued operations, closes the database and joins the thread (up to 5 s).
    ~ConnectionStore() override;

    // QStandardPaths::AppDataLocation/quarry.db
    static QString defaultDatabasePath();

public slots:
    void load(); // -> loaded(list) | failed(Error)
    // Inserts or updates (by id; a null id gets a new UUID). Needs non-empty name and providerId.
    // `secretFieldKeys` = keys of the provider's ConnectionField::Kind::Secret fields: they are
    // removed from `values` before writing, as is any key named "password".
    // lastUsedAt of the argument is ignored (use touch()). -> saved(profile as stored) | failed
    void save(quarry::ConnectionProfile profile, const QStringList &secretFieldKeys = {});
    void remove(QUuid id); // -> removed(id) (also if it did not exist) | failed(Error)
    void touch(QUuid id); // sets lastUsedAt = now -> saved(profile) | failed(NotFound)

signals:
    void loaded(QList<quarry::ConnectionProfile> profiles);
    void saved(quarry::ConnectionProfile profile);
    void removed(QUuid id);
    void failed(quarry::Error error); // not tied to an operation: the UI shows it as is

private:
    template<typename F> void post(F &&task);

    QThread *m_thread = nullptr;
    ConnectionStoreWorker *m_worker = nullptr; // lives on m_thread, deleted there
};

} // namespace quarry
