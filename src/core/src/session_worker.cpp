#include "session_worker.h"

namespace quarry {

namespace {

constexpr int kDefaultFetchSize = 500;

} // namespace

SessionWorker::SessionWorker(std::unique_ptr<IConnection> connection,
                             std::shared_ptr<std::atomic<int>> inflight,
                             std::shared_ptr<std::atomic<bool>> execRunning)
    : m_connection(std::move(connection))
    , m_inflight(std::move(inflight))
    , m_execRunning(std::move(execRunning))
{ }

SessionWorker::~SessionWorker()
{
    m_stream.reset();
    if (m_connection && m_connection->isOpen())
        m_connection->close();
}

// Every operation ends with exactly one terminal signal, and the in-flight count drops
// just before it: a handler reacting to that signal (e.g. fetchMore) must see an idle session.
void SessionWorker::endOperation()
{
    m_execRunning->store(false);
    m_inflight->fetch_sub(1);
}

void SessionWorker::reportError(Error error)
{
    m_stream.reset();
    endOperation();
    if (error.kind == ErrorKind::Cancelled)
        emit cancelled();
    else
        emit failed(std::move(error));
}

void SessionWorker::open(const CancellationToken &token)
{
    if (token.isCancelled()) {
        m_openFailed = true;
        endOperation();
        emit cancelled();
        return;
    }
    auto result = m_connection->open(token);
    m_openFailed = !result;
    if (!result)
        reportError(std::move(result).error());
    else {
        endOperation();
        emit opened(std::move(result).value());
    }
}

void SessionWorker::execute(const Statement &statement, const CancellationToken &token)
{
    m_stream.reset();
    m_rows = 0;
    m_elapsedMs = 0;
    if (token.isCancelled()) {
        endOperation();
        emit cancelled();
        return;
    }
    if (m_openFailed) { // the open() queued before this statement did not succeed
        endOperation();
        emit failed(Error { ErrorKind::Connection,
                            tr("The connection is not open."),
                            tr("The preceding open request failed or was cancelled."),
                            {},
                            {} });
        return;
    }
    m_execRunning->store(true);
    emit started();
    QElapsedTimer timer;
    timer.start();
    auto result = m_connection->execute(statement, token);
    if (!result) {
        reportError(std::move(result).error());
        return;
    }
    m_stream = std::move(result).value();
    emit columnsReady(m_stream->columns());
    fetchBatch(statement.fetchSize > 0 ? statement.fetchSize : kDefaultFetchSize, token, timer);
}

void SessionWorker::fetchMore(qsizetype maxRows, const CancellationToken &token)
{
    if (token.isCancelled()) {
        endOperation();
        emit cancelled();
        return;
    }
    m_execRunning->store(true);
    QElapsedTimer timer;
    timer.start();
    if (!m_stream) { // already at the end (or nothing executed): nothing more to read
        endOperation();
        emit finished(ExecutionStats { m_elapsedMs, m_rows, std::nullopt, {}, true });
        return;
    }
    fetchBatch(maxRows > 0 ? maxRows : kDefaultFetchSize, token, timer);
}

void SessionWorker::fetchBatch(qsizetype maxRows, const CancellationToken &token,
                               QElapsedTimer &timer)
{
    auto result = m_stream->fetch(maxRows, token);
    if (!result) {
        reportError(std::move(result).error());
        return;
    }
    auto batch = std::make_shared<const ResultBatch>(std::move(result).value());
    m_rows += batch->rowCount;
    if (batch->rowCount > 0)
        emit batchReady(batch);
    m_elapsedMs += timer.elapsed();
    ExecutionStats stats { m_elapsedMs, m_rows, m_stream->affectedRows(), m_stream->notices(),
                           m_stream->atEnd() };
    if (stats.atEnd)
        m_stream.reset(); // releases the connection as soon as the result is exhausted
    endOperation();
    emit finished(std::move(stats));
}

void SessionWorker::close()
{
    m_stream.reset();
    m_connection->close();
    endOperation();
    emit closed();
}

} // namespace quarry
