#pragma once

#include <quarry/cancellation.h>
#include <quarry/provider_api.h>
#include <quarry/session.h>

#include <QElapsedTimer>
#include <QObject>

#include <atomic>
#include <memory>

namespace quarry {

// Private to Session. Lives on the session thread and owns the IConnection and the open stream.
// Methods are only called through queued invocations from Session.
class SessionWorker : public QObject
{
    Q_OBJECT

public:
    SessionWorker(std::unique_ptr<IConnection> connection,
                  std::shared_ptr<std::atomic<int>> inflight,
                  std::shared_ptr<std::atomic<bool>> execRunning);
    ~SessionWorker() override;

    IConnection *connection() const { return m_connection.get(); }

    void open(const CancellationToken &token);
    void execute(const Statement &statement, const CancellationToken &token);
    void fetchMore(qsizetype maxRows, const CancellationToken &token);
    void children(quint64 requestId, const ObjectPath &parent, const CancellationToken &token);
    void close();

signals:
    void childrenReady(quint64 requestId, quarry::ObjectPath parent,
                       std::vector<quarry::SchemaNode> nodes);
    void childrenFailed(quint64 requestId, quarry::ObjectPath parent, quarry::Error error);
    void opened(quarry::ServerInfo info);
    void started();
    void columnsReady(std::vector<quarry::ColumnInfo> columns);
    void batchReady(std::shared_ptr<const quarry::ResultBatch> batch);
    void finished(quarry::ExecutionStats stats);
    void failed(quarry::Error error);
    void cancelled();
    void closed();

private:
    void fetchBatch(qsizetype maxRows, const CancellationToken &token, QElapsedTimer &timer);
    void endOperation();
    void reportError(Error error);

    std::unique_ptr<IConnection> m_connection;
    std::unique_ptr<IResultStream> m_stream; // declared after the connection: destroyed first
    std::shared_ptr<std::atomic<int>> m_inflight;
    std::shared_ptr<std::atomic<bool>> m_execRunning; // an execute/fetch is inside the provider
    bool m_openFailed = false; // a failed or cancelled open() makes queued executes fail
    qint64 m_elapsedMs = 0;
    qint64 m_rows = 0;
};

} // namespace quarry
