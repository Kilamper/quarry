#include "connection_store_worker.h"

#include <QLoggingCategory>
#include <QStandardPaths>
#include <QThread>

namespace quarry {

Q_LOGGING_CATEGORY(lcConnectionStore, "quarry.connectionstore")

namespace {
constexpr int kShutdownTimeoutMs = 5000;
}

ConnectionStore::ConnectionStore(QString databasePath, QObject *parent)
    : QObject(parent)
    , m_thread(new QThread) // no parent: must outlive this object if the join times out
{
    qRegisterMetaType<quarry::Error>();
    qRegisterMetaType<quarry::ConnectionProfile>();
    qRegisterMetaType<QList<quarry::ConnectionProfile>>();

    m_worker = new ConnectionStoreWorker(std::move(databasePath));
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    const auto q = Qt::QueuedConnection;
    connect(m_worker, &ConnectionStoreWorker::loaded, this, &ConnectionStore::loaded, q);
    connect(m_worker, &ConnectionStoreWorker::saved, this, &ConnectionStore::saved, q);
    connect(m_worker, &ConnectionStoreWorker::removed, this, &ConnectionStore::removed, q);
    connect(m_worker, &ConnectionStoreWorker::failed, this, &ConnectionStore::failed, q);
    m_thread->start();
}

ConnectionStore::~ConnectionStore()
{
    // Queued after every pending operation, so nothing already requested is lost.
    post([w = m_worker] {
        w->closeDatabase();
        QThread::currentThread()->quit();
    });
    if (m_thread->wait(kShutdownTimeoutMs)) {
        delete m_thread;
    } else {
        qCWarning(lcConnectionStore) << "Store thread did not stop within" << kShutdownTimeoutMs
                                     << "ms; leaking it until the disk call returns";
        connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    }
}

QString ConnectionStore::defaultDatabasePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/quarry.db");
}

template<typename F> void ConnectionStore::post(F &&task)
{
    QMetaObject::invokeMethod(m_worker, std::forward<F>(task), Qt::QueuedConnection);
}

void ConnectionStore::load()
{
    post([w = m_worker] { w->load(); });
}

void ConnectionStore::save(ConnectionProfile profile, const QStringList &secretFieldKeys)
{
    post([w = m_worker, profile = std::move(profile), secretFieldKeys] {
        w->save(profile, secretFieldKeys);
    });
}

void ConnectionStore::remove(QUuid id)
{
    post([w = m_worker, id] { w->remove(id); });
}

void ConnectionStore::touch(QUuid id)
{
    post([w = m_worker, id] { w->touch(id); });
}

} // namespace quarry
