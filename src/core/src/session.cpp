#include "session_worker.h"

#include <quarry/session.h>

#include <QLoggingCategory>
#include <QMetaObject>
#include <QThread>

Q_LOGGING_CATEGORY(lcSession, "quarry.session")

namespace quarry {

namespace {

constexpr int kShutdownTimeoutMs = 5000;

void registerMetaTypes()
{
    static const bool done = [] {
        qRegisterMetaType<quarry::Error>();
        qRegisterMetaType<quarry::ServerInfo>();
        qRegisterMetaType<quarry::ExecutionStats>();
        qRegisterMetaType<std::vector<quarry::ColumnInfo>>();
        qRegisterMetaType<std::shared_ptr<const quarry::ResultBatch>>();
        qRegisterMetaType<quarry::Statement>();
        qRegisterMetaType<quarry::ObjectPath>();
        qRegisterMetaType<std::vector<quarry::SchemaNode>>();
        return true;
    }();
    Q_UNUSED(done)
}

} // namespace

Session::Session(std::unique_ptr<IConnection> connection, QObject *parent)
    : QObject(parent)
    , m_thread(new QThread) // no parent: must outlive this object if the join times out
    , m_execRunning(std::make_shared<std::atomic<bool>>(false))
    , m_inflight(std::make_shared<std::atomic<int>>(0))
{
    registerMetaTypes();
    m_worker = new SessionWorker(std::move(connection), m_inflight, m_execRunning);
    m_connection = m_worker->connection();
    m_worker->moveToThread(m_thread);

    // The worker (and with it the IConnection) is destroyed on its own thread when the
    // thread ends, so the connection is never touched from the UI thread after open().
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    const auto q = Qt::QueuedConnection;
    connect(m_worker, &SessionWorker::opened, this, &Session::opened, q);
    connect(m_worker, &SessionWorker::started, this, &Session::started, q);
    connect(m_worker, &SessionWorker::columnsReady, this, &Session::columnsReady, q);
    connect(m_worker, &SessionWorker::batchReady, this, &Session::batchReady, q);
    connect(m_worker, &SessionWorker::finished, this, &Session::finished, q);
    connect(m_worker, &SessionWorker::failed, this, &Session::failed, q);
    connect(m_worker, &SessionWorker::cancelled, this, &Session::cancelled, q);
    connect(m_worker, &SessionWorker::closed, this, &Session::closed, q);
    connect(m_worker, &SessionWorker::childrenReady, this, &Session::childrenReady, q);
    connect(m_worker, &SessionWorker::childrenFailed, this, &Session::childrenFailed, q);
    m_thread->start();
}

Session::~Session()
{
    cancel();
    m_thread->quit();
    if (m_thread->wait(kShutdownTimeoutMs)) {
        delete m_thread;
    } else {
        qCWarning(lcSession) << "Session thread did not stop within" << kShutdownTimeoutMs
                             << "ms; leaking it until the provider returns";
        connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    }
}

template<typename F> void Session::post(F &&task)
{
    m_inflight->fetch_add(1);
    QMetaObject::invokeMethod(m_worker, std::forward<F>(task), Qt::QueuedConnection);
}

void Session::open()
{
    m_openSource = CancellationSource();
    const auto token = m_openSource.token();
    post([w = m_worker, token] { w->open(token); });
}

void Session::execute(const Statement &statement)
{
    // Only an execute/fetch is cancelled here; a queued open() keeps its own token. The
    // provider is only poked if one is actually inside it, so a queued open is never hit.
    m_execSource.cancel();
    if (m_execRunning->load())
        m_connection->requestCancel();
    m_execSource = CancellationSource(); // fresh flag: the old one stays cancelled for the old op
    const auto token = m_execSource.token();
    post([w = m_worker, statement, token] { w->execute(statement, token); });
}

void Session::fetchMore(qsizetype maxRows)
{
    if (m_inflight->load() > 0)
        return;
    m_execSource = CancellationSource(); // idle: an earlier cancel() must not poison this
    const auto token = m_execSource.token();
    post([w = m_worker, maxRows, token] { w->fetchMore(maxRows, token); });
}

quint64 Session::children(const ObjectPath &parent)
{
    const quint64 id = ++m_lastRequestId;
    const auto token = m_metaSource.token();
    post([w = m_worker, id, parent, token] { w->children(id, parent, token); });
    return id;
}

void Session::cancel()
{
    if (m_inflight->load() == 0)
        return; // idle: a stale requestCancel() could abort the next statement
    m_openSource.cancel();
    m_execSource.cancel();
    m_metaSource.cancel();
    m_metaSource = CancellationSource(); // later children() requests must not start cancelled
    m_connection->requestCancel();
}

void Session::close()
{
    if (m_inflight->load() > 0)
        cancel();
    post([w = m_worker] { w->close(); });
}

} // namespace quarry
