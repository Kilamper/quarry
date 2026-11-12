#include <quarry/session.h>

#include <QSignalSpy>
#include <QtTest>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>

using namespace quarry;

namespace {

// State shared between the test (main thread) and the fake that the Session owns.
struct FakeState
{
    std::atomic<qint64> totalRows { 1000 };
    std::atomic<bool> blockOnExecute { false };
    std::atomic<int> fetchDelayMs { 0 };
    std::optional<Error> executeError; // set before creating the Session
    std::atomic<int> openDelayMs { 0 };
    std::optional<Error> openError; // set before creating the Session
    std::atomic<int> cancelRequests { 0 };
    std::atomic<int> executeCalls { 0 };
    std::atomic<bool> cancelRequested { false };
    std::atomic<bool> inExecute { false };
    std::atomic<bool> destroyed { false };
    std::atomic<QThread *> destroyThread { nullptr };

    void record()
    {
        std::lock_guard lock(m_mutex);
        m_threads.push_back(QThread::currentThread());
    }
    std::vector<QThread *> threads()
    {
        std::lock_guard lock(m_mutex);
        return m_threads;
    }

private:
    std::mutex m_mutex;
    std::vector<QThread *> m_threads;
};

class FakeStream : public IResultStream
{
public:
    explicit FakeStream(std::shared_ptr<FakeState> state)
        : m_state(std::move(state))
    {
        m_columns.push_back({ QStringLiteral("id"), ValueType::Int64, QStringLiteral("int8"), false,
                              std::nullopt });
        m_columns.push_back({ QStringLiteral("label"), ValueType::Text, QStringLiteral("text"),
                              true, std::nullopt });
    }

    const std::vector<ColumnInfo> &columns() const override { return m_columns; }

    Result<ResultBatch> fetch(qsizetype maxRows, const CancellationToken &token) override
    {
        m_state->record();
        if (m_state->fetchDelayMs > 0)
            QThread::msleep(static_cast<unsigned long>(m_state->fetchDelayMs.load()));
        if (token.isCancelled())
            return fail(Error { ErrorKind::Cancelled, QStringLiteral("Cancelled"), {}, {}, {} });
        const qint64 n = std::min<qint64>(maxRows, m_state->totalRows - m_pos);
        ResultBatch batch;
        batch.rowCount = n;
        batch.columns.resize(2);
        for (qint64 i = 0; i < n; ++i) {
            batch.columns[0].append(int64_t(m_pos + i));
            batch.columns[1].appendBytes(QByteArray::number(m_pos + i), ValueType::Text);
        }
        m_pos += n;
        return batch;
    }
    bool atEnd() const override { return m_pos >= m_state->totalRows; }
    std::optional<qint64> affectedRows() const override { return std::nullopt; }
    QStringList notices() const override { return {}; }
    Result<bool> nextResult(const CancellationToken &) override { return false; }

private:
    std::shared_ptr<FakeState> m_state;
    std::vector<ColumnInfo> m_columns;
    qint64 m_pos = 0;
};

class FakeConnection : public IConnection
{
public:
    explicit FakeConnection(std::shared_ptr<FakeState> state)
        : m_state(std::move(state))
    { }
    ~FakeConnection() override
    {
        m_state->destroyThread = QThread::currentThread();
        m_state->destroyed = true;
    }

    Result<ServerInfo> open(const CancellationToken &) override
    {
        m_state->record();
        if (m_state->openDelayMs > 0)
            QThread::msleep(static_cast<unsigned long>(m_state->openDelayMs.load()));
        if (m_state->openError)
            return fail(*m_state->openError);
        m_open = true;
        return ServerInfo { QStringLiteral("Fake"), QStringLiteral("1.0") };
    }
    void close() override
    {
        m_state->record();
        m_open = false;
    }
    bool isOpen() const override { return m_open; }
    Result<std::vector<SchemaNode>> children(const ObjectPath &, const CancellationToken &) override
    {
        return std::vector<SchemaNode> {};
    }
    Result<TableDetails> describe(const ObjectPath &, const CancellationToken &) override
    {
        return TableDetails {};
    }

    Result<std::unique_ptr<IResultStream>> execute(const Statement &,
                                                   const CancellationToken &token) override
    {
        m_state->record();
        ++m_state->executeCalls;
        m_state->cancelRequested = false;
        if (m_state->executeError)
            return fail(*m_state->executeError);
        if (m_state->blockOnExecute) {
            m_state->inExecute = true;
            while (!token.isCancelled() && !m_state->cancelRequested)
                QThread::msleep(1);
            m_state->inExecute = false;
            return fail(Error { ErrorKind::Cancelled, QStringLiteral("Cancelled"), {}, {}, {} });
        }
        return std::make_unique<FakeStream>(m_state);
    }

    // Thread-safe by contract; deliberately not recorded as a worker-thread call.
    void requestCancel() override
    {
        ++m_state->cancelRequests;
        m_state->cancelRequested = true;
    }

    Result<void> begin() override { return {}; }
    Result<void> commit() override { return {}; }
    Result<void> rollback() override { return {}; }

private:
    std::shared_ptr<FakeState> m_state;
    bool m_open = false;
};

std::unique_ptr<Session> makeSession(const std::shared_ptr<FakeState> &state)
{
    return std::make_unique<Session>(std::make_unique<FakeConnection>(state));
}

} // namespace

class TstSession : public QObject
{
    Q_OBJECT

private slots:
    void streamsFirstBatchThenFetchMore();
    void streamsMillionRowsWithoutBlockingMainThread();
    void cancelOfBlockingQueryIsFast();
    void providerErrorBecomesFailed();
    void destroyWhileBlockedIsPrompt();
    void connectionCalledOnWorkerThread();
    void newExecuteCancelsRunningOne();
    void executeRightAfterOpenWaitsForOpen();
    void executeAfterFailedOpenFails();
    void idleCancelDoesNotTouchConnection();
};

void TstSession::streamsFirstBatchThenFetchMore()
{
    auto state = std::make_shared<FakeState>();
    state->totalRows = 1200;
    auto session = makeSession(state);
    QSignalSpy opened(session.get(), &Session::opened);
    QSignalSpy started(session.get(), &Session::started);
    QSignalSpy columns(session.get(), &Session::columnsReady);
    QSignalSpy batches(session.get(), &Session::batchReady);
    QSignalSpy finished(session.get(), &Session::finished);

    session->open();
    QTRY_COMPARE(opened.count(), 1);
    QCOMPARE(opened.at(0).at(0).value<ServerInfo>().product, QStringLiteral("Fake"));

    session->execute(Statement { QStringLiteral("select"), {}, 500 });
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(started.count(), 1);
    QCOMPARE(columns.count(), 1);
    QCOMPARE(columns.at(0).at(0).value<std::vector<ColumnInfo>>().size(), size_t(2));
    QCOMPARE(batches.count(), 1);
    auto b0 = batches.at(0).at(0).value<std::shared_ptr<const ResultBatch>>();
    QCOMPARE(b0->rowCount, 500);
    QCOMPARE(b0->columns[0].int64At(499), 499);
    auto s0 = finished.at(0).at(0).value<ExecutionStats>();
    QVERIFY(!s0.atEnd);
    QCOMPARE(s0.rowsFetched, 500);

    session->fetchMore(500);
    QTRY_COMPARE(finished.count(), 2);
    auto b1 = batches.at(1).at(0).value<std::shared_ptr<const ResultBatch>>();
    QCOMPARE(b1->rowCount, 500);
    QCOMPARE(b1->columns[0].int64At(0), 500);

    session->fetchMore(500);
    QTRY_COMPARE(finished.count(), 3);
    auto b2 = batches.at(2).at(0).value<std::shared_ptr<const ResultBatch>>();
    QCOMPARE(b2->rowCount, 200);
    QCOMPARE(b2->columns[1].bytesAt(199), QByteArrayView("1199"));
    auto s2 = finished.at(2).at(0).value<ExecutionStats>();
    QVERIFY(s2.atEnd);
    QCOMPARE(s2.rowsFetched, 1200);
}

void TstSession::streamsMillionRowsWithoutBlockingMainThread()
{
    auto state = std::make_shared<FakeState>();
    state->totalRows = 1'000'000;
    state->fetchDelayMs = 2; // keeps the stream busy long enough to observe the main thread
    auto session = makeSession(state);

    int ticks = 0;
    QTimer ticker;
    ticker.setInterval(5);
    connect(&ticker, &QTimer::timeout, this, [&] { ++ticks; });
    ticker.start();

    qint64 rows = 0;
    bool done = false;
    connect(session.get(), &Session::batchReady, this,
            [&](const std::shared_ptr<const ResultBatch> &batch) { rows += batch->rowCount; });
    connect(session.get(), &Session::finished, this, [&](const ExecutionStats &stats) {
        if (stats.atEnd)
            done = true;
        else
            session->fetchMore(10'000);
    });

    QElapsedTimer clock;
    clock.start();
    session->execute(Statement { QStringLiteral("select"), {}, 500 });
    QTRY_VERIFY_WITH_TIMEOUT(done, 30'000);
    qInfo() << "1M rows streamed in" << clock.elapsed() << "ms," << ticks << "main-thread ticks";
    QCOMPARE(rows, 1'000'000);
    QVERIFY2(ticks >= 5, "main thread did not keep processing events while streaming");
}

void TstSession::cancelOfBlockingQueryIsFast()
{
    auto state = std::make_shared<FakeState>();
    state->blockOnExecute = true;
    auto session = makeSession(state);
    QSignalSpy cancelled(session.get(), &Session::cancelled);
    QSignalSpy failed(session.get(), &Session::failed);

    session->execute(Statement { QStringLiteral("select pg_sleep(100)"), {}, 500 });
    QTRY_VERIFY(state->inExecute);

    QElapsedTimer clock;
    clock.start();
    session->cancel();
    QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1, 1000);
    QVERIFY2(clock.elapsed() < 200, qPrintable(QString::number(clock.elapsed())));
    QCOMPARE(failed.count(), 0);
}

void TstSession::providerErrorBecomesFailed()
{
    auto state = std::make_shared<FakeState>();
    state->executeError = Error { ErrorKind::Syntax, QStringLiteral("Syntax error"),
                                  QStringLiteral("near \"selec\""), QStringLiteral("42601"), 1 };
    auto session = makeSession(state);
    QSignalSpy failed(session.get(), &Session::failed);
    QSignalSpy cancelled(session.get(), &Session::cancelled);

    session->execute(Statement { QStringLiteral("selec"), {}, 500 });
    QTRY_COMPARE(failed.count(), 1);
    const auto error = failed.at(0).at(0).value<Error>();
    QCOMPARE(error.kind, ErrorKind::Syntax);
    QCOMPARE(error.message, QStringLiteral("Syntax error"));
    QCOMPARE(error.detail, QStringLiteral("near \"selec\""));
    QCOMPARE(error.sqlState, QStringLiteral("42601"));
    QCOMPARE(error.position.value_or(-1), 1);
    QCOMPARE(cancelled.count(), 0);
}

void TstSession::destroyWhileBlockedIsPrompt()
{
    auto state = std::make_shared<FakeState>();
    state->blockOnExecute = true;
    auto session = makeSession(state);
    session->execute(Statement { QStringLiteral("select pg_sleep(100)"), {}, 500 });
    QTRY_VERIFY(state->inExecute);

    QElapsedTimer clock;
    clock.start();
    session.reset();
    QVERIFY2(clock.elapsed() < 1000, qPrintable(QString::number(clock.elapsed())));
    QVERIFY(state->destroyed); // connection destroyed on the worker thread before wait() returned
}

void TstSession::connectionCalledOnWorkerThread()
{
    auto state = std::make_shared<FakeState>();
    state->totalRows = 10;
    std::vector<QThread *> threads;
    {
        auto session = makeSession(state);
        QSignalSpy opened(session.get(), &Session::opened);
        QSignalSpy finished(session.get(), &Session::finished);
        QSignalSpy closed(session.get(), &Session::closed);
        session->open();
        session->execute(Statement { QStringLiteral("select"), {}, 500 });
        QTRY_COMPARE(finished.count(), 1);
        session->close();
        QTRY_COMPARE(closed.count(), 1);
    }
    QVERIFY(state->destroyed);
    threads = state->threads();
    QVERIFY2(threads.size() >= 4,
             qPrintable(QString::number(threads.size()))); // open, execute, fetch, close
    QThread *worker = threads.front();
    QVERIFY(worker != QThread::currentThread());
    for (QThread *t : threads)
        QCOMPARE(t, worker);
    QCOMPARE(state->destroyThread.load(), worker); // destructor too
}

void TstSession::newExecuteCancelsRunningOne()
{
    auto state = std::make_shared<FakeState>();
    state->blockOnExecute = true;
    auto session = makeSession(state);
    QSignalSpy cancelled(session.get(), &Session::cancelled);
    QSignalSpy finished(session.get(), &Session::finished);

    session->execute(Statement { QStringLiteral("slow"), {}, 500 });
    QTRY_VERIFY(state->inExecute);
    state->blockOnExecute = false;
    session->execute(Statement { QStringLiteral("fast"), {}, 500 });
    QTRY_COMPARE(cancelled.count(), 1);
    QTRY_COMPARE(finished.count(), 1);
}

void TstSession::executeRightAfterOpenWaitsForOpen()
{
    auto state = std::make_shared<FakeState>();
    state->openDelayMs = 100;
    state->totalRows = 10;
    auto session = makeSession(state);
    QSignalSpy opened(session.get(), &Session::opened);
    QSignalSpy batches(session.get(), &Session::batchReady);
    QSignalSpy finished(session.get(), &Session::finished);
    QSignalSpy cancelled(session.get(), &Session::cancelled);
    QSignalSpy failed(session.get(), &Session::failed);

    session->open();
    session->execute(Statement { QStringLiteral("select"), {}, 500 });
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(opened.count(), 1);
    QCOMPARE(batches.count(), 1);
    QCOMPARE(cancelled.count(), 0);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(state->cancelRequests.load(), 0);
}

void TstSession::executeAfterFailedOpenFails()
{
    auto state = std::make_shared<FakeState>();
    state->openError = Error { ErrorKind::Connection, QStringLiteral("down"), {}, {}, {} };
    auto session = makeSession(state);
    QSignalSpy failed(session.get(), &Session::failed);

    session->open();
    session->execute(Statement { QStringLiteral("select"), {}, 500 });
    QTRY_COMPARE(failed.count(), 2);
    QCOMPARE(failed.at(0).at(0).value<Error>().message, QStringLiteral("down"));
    QCOMPARE(failed.at(1).at(0).value<Error>().message,
             QStringLiteral("The connection is not open."));
    QCOMPARE(state->executeCalls.load(), 0);
}

void TstSession::idleCancelDoesNotTouchConnection()
{
    auto state = std::make_shared<FakeState>();
    auto session = makeSession(state);
    session->cancel();
    QCOMPARE(state->cancelRequests.load(), 0);
}

QTEST_GUILESS_MAIN(TstSession)
#include "tst_session.moc"
