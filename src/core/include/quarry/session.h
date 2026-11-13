#pragma once

#include <quarry/cancellation.h>
#include <quarry/provider_api.h>
#include <quarry/result.h>
#include <quarry/result_batch.h>

#include <QMetaType>
#include <QObject>
#include <QStringList>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

class QThread;

namespace quarry {

class SessionWorker;

struct ExecutionStats
{
    qint64 elapsedMs = 0; // accumulated over execute + fetchMore calls
    qint64 rowsFetched = 0; // accumulated rows streamed so far
    std::optional<qint64> affectedRows;
    QStringList notices;
    bool atEnd = false; // false: more rows can be requested with fetchMore()
};

// UI-thread facade over one IConnection that lives on its own worker thread.
// All methods are non-blocking; results arrive as queued signals. The connection is only
// touched on the worker thread, except IConnection::requestCancel() (see cancel()).
class Session : public QObject
{
    Q_OBJECT

public:
    explicit Session(std::unique_ptr<IConnection> connection, QObject *parent = nullptr);
    // Cancels the running operation, stops the thread and waits up to 5 s for it.
    ~Session() override;

public slots:
    void open();
    // Single statement. A running execute/fetchMore is cancelled first (it ends with
    // cancelled()). A pending open() is NOT cancelled: this statement runs after it, or fails
    // with "The connection is not open." if the open failed. Fetches the first `fetchSize` rows.
    void execute(const quarry::Statement &statement);
    // Next batch of the current result. Ignored while another operation is running.
    void fetchMore(qsizetype maxRows);
    // Direct children of `parent` (IConnection::children), for the lazy schema tree. Returns an
    // id > 0, unique per Session; the answer arrives as childrenReady/childrenFailed with it.
    // Queued in order with the other operations; does NOT cancel a running execute. Called before
    // open() finishes it runs after it; if the open failed (or never happened) it fails with
    // "The connection is not open.". cancel() and close() end every pending or running request
    // with childrenFailed(ErrorKind::Cancelled), exactly once per request.
    quint64 children(const quarry::ObjectPath &parent);
    // Cancels the tokens of all queued/running operations and, if something is running, calls
    // requestCancel() directly from the calling thread. Idle session: does nothing at all
    // (requestCancel() is not called, no signal is emitted).
    void cancel();
    // Cancels whatever is running, then closes the connection (emits closed()).
    void close();

signals:
    void opened(quarry::ServerInfo info);
    void started();
    void columnsReady(std::vector<quarry::ColumnInfo> columns);
    void batchReady(std::shared_ptr<const quarry::ResultBatch> batch);
    void finished(quarry::ExecutionStats stats);
    void failed(quarry::Error error);
    void cancelled();
    void closed();
    void childrenReady(quint64 requestId, quarry::ObjectPath parent,
                       std::vector<quarry::SchemaNode> nodes);
    void childrenFailed(quint64 requestId, quarry::ObjectPath parent, quarry::Error error);

private:
    template<typename F> void post(F &&task);

    QThread *m_thread = nullptr;
    SessionWorker *m_worker = nullptr; // lives on m_thread, deleted there
    IConnection *m_connection = nullptr; // owned by m_worker; only for requestCancel()
    CancellationSource m_openSource; // token of the latest open()
    CancellationSource m_execSource; // token of the latest execute()/fetchMore()
    CancellationSource m_metaSource; // shared by the children() requests since the last cancel()
    quint64 m_lastRequestId = 0;
    std::shared_ptr<std::atomic<bool>> m_execRunning; // execute/fetch is inside the provider
    std::shared_ptr<std::atomic<int>> m_inflight; // operations queued or running
};

} // namespace quarry

Q_DECLARE_METATYPE(quarry::Error)
Q_DECLARE_METATYPE(quarry::ServerInfo)
Q_DECLARE_METATYPE(quarry::ExecutionStats)
Q_DECLARE_METATYPE(std::vector<quarry::ColumnInfo>)
Q_DECLARE_METATYPE(std::shared_ptr<const quarry::ResultBatch>)
Q_DECLARE_METATYPE(quarry::Statement)
Q_DECLARE_METATYPE(quarry::ObjectPath)
Q_DECLARE_METATYPE(std::vector<quarry::SchemaNode>)
