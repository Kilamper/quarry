#include "provider_conformance.h"

#include <quarry/provider_registry.h>
#include <quarry/session.h>

#include <QSignalSpy>
#include <QtTest>

#include <algorithm>
#include <iterator>

using namespace quarry;
using namespace quarry::conformance;

namespace {

class SqliteFixture : public Fixture
{
public:
    SqliteFixture(IDatabaseProvider &provider, QString file)
        : m_provider(provider)
        , m_file(std::move(file))
    { }

    IDatabaseProvider &provider() override { return m_provider; }
    ConnectionConfig config() override
    {
        ConnectionConfig config;
        config.values.insert(QStringLiteral("file"), m_file);
        return config;
    }
    QString sampleSchemaSql() const override
    {
        return QStringLiteral(
            "CREATE TABLE authors(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE);\n"
            "CREATE TABLE books(id INTEGER PRIMARY KEY, title TEXT NOT NULL,\n"
            "  author_id INTEGER NOT NULL REFERENCES authors(id), price REAL, cover BLOB);\n"
            "CREATE INDEX idx_books_title ON books(title); -- a comment; with a semicolon\n"
            "CREATE VIEW v_titles AS SELECT id, title FROM books;\n");
    }
    QString slowQuerySql() const override
    {
        return QStringLiteral("WITH RECURSIVE r(n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM r) "
                              "SELECT count(*) FROM r");
    }
    QString seriesSql(int count) const override
    {
        return QStringLiteral("WITH RECURSIVE s(n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM s "
                              "WHERE n < %1) SELECT n FROM s")
            .arg(count);
    }
    ObjectPath booksPath() const override
    {
        return { { QStringLiteral("main"), QStringLiteral("books") }, ObjectKind::Table };
    }
    ObjectPath viewPath() const override
    {
        return { { QStringLiteral("main"), QStringLiteral("v_titles") }, ObjectKind::View };
    }
    std::vector<SplitCase> extraSplitCases() const override
    {
        const QString trigger = QStringLiteral("CREATE TRIGGER trg AFTER INSERT ON books BEGIN\n"
                                               "  UPDATE books SET price = 1 WHERE id = new.id;\n"
                                               "  SELECT CASE WHEN 1 THEN 2 END;\n"
                                               "END");
        return {
            { QStringLiteral("SELECT `a;b` FROM t; SELECT [c;d] FROM t"),
              { QStringLiteral("SELECT `a;b` FROM t"), QStringLiteral("SELECT [c;d] FROM t") } },
            { QStringLiteral("SELECT \"a\"\"; b\"; SELECT 2"),
              { QStringLiteral("SELECT \"a\"\"; b\""), QStringLiteral("SELECT 2") } },
            { trigger + QStringLiteral(";\nSELECT 1;"), { trigger, QStringLiteral("SELECT 1") } },
            { QStringLiteral("CREATE TEMP TRIGGER t2 AFTER DELETE ON books BEGIN SELECT 1; END; "
                             "SELECT 2"),
              { QStringLiteral("CREATE TEMP TRIGGER t2 AFTER DELETE ON books BEGIN SELECT 1; END"),
                QStringLiteral("SELECT 2") } },
            { QStringLiteral("BEGIN; SELECT 1; END"),
              { QStringLiteral("BEGIN"), QStringLiteral("SELECT 1"), QStringLiteral("END") } },
            { QStringLiteral("SELECT 1; /* unterminated ; comment"),
              { QStringLiteral("SELECT 1") } },
        };
    }

private:
    IDatabaseProvider &m_provider;
    QString m_file;
};

const QString kSlowSql = QStringLiteral("WITH RECURSIVE r(n) AS (SELECT 1 UNION ALL SELECT n + 1 "
                                        "FROM r) SELECT count(*) FROM r");

} // namespace

class TstSqliteProvider : public ProviderConformanceTest
{
    Q_OBJECT

protected:
    std::unique_ptr<Fixture> createFixture() override
    {
        return std::make_unique<SqliteFixture>(
            *m_provider, m_dir.filePath(QStringLiteral("db%1.sqlite").arg(++m_counter)));
    }

private slots:
    void initTestCase();

    void pluginMetadata();
    void mixedTypeColumn();
    void declaredTypesMapToLogicalTypes();
    void sourceTableIsReported();
    void nonAsciiPath();
    void memoryDatabase();
    void readOnlyOnMissingFileFails();
    void readOnlyRejectsWrites();
    void multipleStatementsAreRejected();
    void emptyAndCommentOnlyStatements();
    void syntaxErrorPositionIsExact();
    void syntaxErrorPositionIsInUtf16Units();
    void transactions();
    void dialectSpecifics();
    void sessionStreamsMillionRowsWithoutBlockingMainThread();
    void sessionCancelsInfiniteQuery();

private:
    std::unique_ptr<IConnection> memory();
    std::unique_ptr<IConnection> openFile(const QString &file, bool readOnly = false);

    ProviderRegistry m_registry;
    IDatabaseProvider *m_provider = nullptr;
    QTemporaryDir m_dir;
    int m_counter = 0;
};

void TstSqliteProvider::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_registry.scan(QStringLiteral(QUARRY_PLUGIN_DIR));
    QString error;
    m_provider = m_registry.provider(QStringLiteral("sqlite"), &error);
    QVERIFY2(m_provider, qPrintable(error));
}

std::unique_ptr<IConnection> TstSqliteProvider::openFile(const QString &file, bool readOnly)
{
    ConnectionConfig config;
    config.values.insert(QStringLiteral("file"), file);
    config.values.insert(QStringLiteral("readOnly"), readOnly);
    auto connection = m_provider->createConnection(config, {});
    auto info = connection->open({});
    if (!info)
        return nullptr;
    return connection;
}

std::unique_ptr<IConnection> TstSqliteProvider::memory()
{
    return openFile(QStringLiteral(":memory:"));
}

void TstSqliteProvider::pluginMetadata()
{
    QCOMPARE(m_registry.available().size(), size_t(1));
    QCOMPARE(m_registry.available().front().id, QStringLiteral("sqlite"));
    const ProviderInfo info = m_provider->info();
    QCOMPARE(info.id, QStringLiteral("sqlite"));
    QVERIFY(info.caps.testFlag(Capability::Cancel));

    const auto fields = m_provider->connectionFields();
    QCOMPARE(fields.size(), size_t(3));
    QCOMPARE(fields[0].key, QStringLiteral("file"));
    QCOMPARE(fields[0].kind, ConnectionField::Kind::Path);
    QVERIFY(fields[0].required && !fields[0].advanced);
    QCOMPARE(fields[1].key, QStringLiteral("readOnly"));
    QCOMPARE(fields[1].kind, ConnectionField::Kind::Bool);
    QVERIFY(fields[1].advanced);
    QCOMPARE(fields[2].key, QStringLiteral("busyTimeoutMs"));
    QCOMPARE(fields[2].defaultValue.toInt(), 5000);
    QVERIFY(fields[2].advanced);
}

void TstSqliteProvider::mixedTypeColumn()
{
    auto connection = memory();
    QVERIFY(connection);
    const auto d = drain(*connection,
                         QStringLiteral("SELECT 1 UNION ALL SELECT 'a' UNION ALL SELECT x'00ff' "
                                        "UNION ALL SELECT NULL UNION ALL SELECT 2.5"));
    QVERIFY2(d.ok, qPrintable(d.errorText()));
    QCOMPARE(d.rows, 5);
    QCOMPARE(d.columns.size(), size_t(1));
    QCOMPARE(d.columns[0].type, ValueType::Other); // no declared type
    const ColumnBuffer &c = d.batches.front().columns[0];
    QVERIFY(c.isMixed());
    QCOMPARE(c.typeAt(0), ValueType::Int64);
    QCOMPARE(c.int64At(0), 1);
    QCOMPARE(c.typeAt(1), ValueType::Text);
    QCOMPARE(QByteArray(c.bytesAt(1)), QByteArray("a"));
    QCOMPARE(c.typeAt(2), ValueType::Bytes);
    QCOMPARE(QByteArray(c.bytesAt(2)), QByteArray("\x00\xff", 2));
    QCOMPARE(c.typeAt(3), ValueType::Null);
    QVERIFY(c.isNull(3));
    QCOMPARE(c.typeAt(4), ValueType::Double);
    QCOMPARE(c.doubleAt(4), 2.5);

    // A declared INTEGER column holding text: the column type degrades to Other once seen.
    QVERIFY(drain(*connection, QStringLiteral("CREATE TABLE m(v INTEGER)")).ok);
    QVERIFY(drain(*connection, QStringLiteral("INSERT INTO m VALUES (1), ('x')")).ok);
    auto stream = connection->execute(Statement { QStringLiteral("SELECT v FROM m"), {}, 500 }, {});
    QVERIFY(stream);
    QCOMPARE(stream.value()->columns()[0].type, ValueType::Int64);
    auto batch = stream.value()->fetch(500, {});
    QVERIFY(batch);
    QVERIFY(batch.value().columns[0].isMixed());
    QCOMPARE(stream.value()->columns()[0].type, ValueType::Other);
}

void TstSqliteProvider::declaredTypesMapToLogicalTypes()
{
    auto connection = memory();
    QVERIFY(connection);
    QVERIFY(drain(*connection,
                  QStringLiteral("CREATE TABLE t(a INTEGER, b REAL, c TEXT, d BLOB, e BOOLEAN, "
                                 "f DATETIME, g NUMERIC(10,2), h JSON, i VARCHAR(10), j DOUBLE, "
                                 "k)"))
                .ok);
    QVERIFY(drain(*connection,
                  QStringLiteral("INSERT INTO t VALUES (1, 2.5, 'x', x'01', 1, '2026-01-02 03:04', "
                                 "42, '{}', 'v', 1.5, NULL)"))
                .ok);
    const auto d = drain(*connection, QStringLiteral("SELECT * FROM t"));
    QVERIFY2(d.ok, qPrintable(d.errorText()));
    const ValueType expected[]
        = { ValueType::Int64, ValueType::Double,   ValueType::Text,    ValueType::Bytes,
            ValueType::Bool,  ValueType::DateTime, ValueType::Decimal, ValueType::Json,
            ValueType::Text,  ValueType::Double,   ValueType::Other };
    QCOMPARE(d.columns.size(), std::size(expected));
    for (size_t i = 0; i < std::size(expected); ++i)
        QCOMPARE(d.columns[i].type, expected[i]);
    QCOMPARE(d.columns[6].nativeType, QStringLiteral("NUMERIC(10,2)"));

    const ResultBatch &b = d.batches.front();
    QCOMPARE(b.columns[4].typeAt(0), ValueType::Bool);
    QCOMPARE(b.columns[4].int64At(0), 1);
    QCOMPARE(b.columns[5].typeAt(0), ValueType::DateTime);
    QCOMPARE(b.columns[6].typeAt(0), ValueType::Decimal); // stored as an integer, delivered exactly
    QCOMPARE(QByteArray(b.columns[6].bytesAt(0)), QByteArray("42"));
    QCOMPARE(b.columns[7].typeAt(0), ValueType::Json);
    QVERIFY(b.columns[10].isNull(0));
    for (const auto &column : b.columns)
        QVERIFY(!column.isMixed());
}

void TstSqliteProvider::sourceTableIsReported()
{
    auto connection = memory();
    QVERIFY(connection);
    QVERIFY(drain(*connection, QStringLiteral("CREATE TABLE t(id INTEGER, v TEXT)")).ok);
    const auto d = drain(*connection, QStringLiteral("SELECT id, v || 'x' AS e FROM t"));
    QVERIFY2(d.ok, qPrintable(d.errorText()));
    QVERIFY(d.columns[0].sourceTable.has_value());
    const ObjectPath source = d.columns[0].sourceTable.value_or(ObjectPath {});
    QCOMPARE(source.parts, (QStringList { QStringLiteral("main"), QStringLiteral("t") }));
    QVERIFY(!d.columns[1].sourceTable.has_value());
}

void TstSqliteProvider::nonAsciiPath()
{
    const QString file = m_dir.filePath(QStringLiteral("données_日本語_😀.sqlite"));
    {
        auto connection = openFile(file);
        QVERIFY(connection);
        QVERIFY(drain(*connection, QStringLiteral("CREATE TABLE t(v TEXT)")).ok);
        QVERIFY(
            drain(*connection, QStringLiteral("INSERT INTO t VALUES (?)"), { QStringLiteral("ñ") })
                .ok);
    }
    QVERIFY(QFileInfo::exists(file));
    auto connection = openFile(file, true);
    QVERIFY(connection);
    const auto d = drain(*connection, QStringLiteral("SELECT v FROM t"));
    QVERIFY2(d.ok, qPrintable(d.errorText()));
    QCOMPARE(QString::fromUtf8(d.batches.front().columns[0].bytesAt(0)), QStringLiteral("ñ"));
}

void TstSqliteProvider::memoryDatabase()
{
    auto connection = memory();
    QVERIFY(connection);
    QVERIFY(drain(*connection, QStringLiteral("CREATE TABLE t(a)")).ok);
    QVERIFY(drain(*connection, QStringLiteral("INSERT INTO t VALUES (1), (2)")).ok);
    QCOMPARE(drain(*connection, QStringLiteral("SELECT a FROM t")).rows, 2);

    auto info = connection->open({}); // already open
    QVERIFY(!info);
    connection->close();
    QVERIFY(!connection->isOpen());
    const auto closed = drain(*connection, QStringLiteral("SELECT 1"));
    QVERIFY(!closed.ok);
    QCOMPARE(closed.error.kind, ErrorKind::Connection);
}

void TstSqliteProvider::readOnlyOnMissingFileFails()
{
    ConnectionConfig config;
    config.values.insert(QStringLiteral("file"), m_dir.filePath(QStringLiteral("missing.sqlite")));
    config.values.insert(QStringLiteral("readOnly"), true);
    auto connection = m_provider->createConnection(config, {});
    auto info = connection->open({});
    QVERIFY(!info);
    QCOMPARE(std::move(info).error().kind, ErrorKind::NotFound);
    QVERIFY(!QFileInfo::exists(config.values.value(QStringLiteral("file")).toString()));

    // A directory that does not exist (read-write) is a connection error, not a crash.
    config.values.insert(QStringLiteral("readOnly"), false);
    config.values.insert(QStringLiteral("file"), m_dir.filePath(QStringLiteral("nope/x.sqlite")));
    auto other = m_provider->createConnection(config, {});
    auto otherInfo = other->open({});
    QVERIFY(!otherInfo);
    QCOMPARE(std::move(otherInfo).error().kind, ErrorKind::Connection);
}

void TstSqliteProvider::readOnlyRejectsWrites()
{
    const QString file = m_dir.filePath(QStringLiteral("ro.sqlite"));
    {
        auto rw = openFile(file);
        QVERIFY(rw);
        QVERIFY(drain(*rw, QStringLiteral("CREATE TABLE t(a)")).ok);
    }
    auto ro = openFile(file, true);
    QVERIFY(ro);
    QVERIFY(drain(*ro, QStringLiteral("SELECT * FROM t")).ok);
    const auto write = drain(*ro, QStringLiteral("INSERT INTO t VALUES (1)"));
    QVERIFY(!write.ok);
    QCOMPARE(write.error.kind, ErrorKind::Permission);
}

void TstSqliteProvider::multipleStatementsAreRejected()
{
    auto connection = memory();
    QVERIFY(connection);
    const auto d
        = drain(*connection, QStringLiteral("CREATE TABLE t(a); INSERT INTO t VALUES (1)"));
    QVERIFY(!d.ok);
    QCOMPARE(d.error.kind, ErrorKind::Unsupported);
    // Nothing of the first statement ran.
    QVERIFY(!drain(*connection, QStringLiteral("SELECT * FROM t")).ok);
    // Trailing semicolons and comments are not a second statement.
    QVERIFY(drain(*connection, QStringLiteral("SELECT 1; -- done\n /* x */ ;")).ok);
}

void TstSqliteProvider::emptyAndCommentOnlyStatements()
{
    auto connection = memory();
    QVERIFY(connection);
    for (const QString &sql :
         { QString(), QStringLiteral("  "), QStringLiteral("-- nothing"), QStringLiteral(";") }) {
        const auto d = drain(*connection, sql);
        QVERIFY2(d.ok, qPrintable(sql));
        QCOMPARE(d.rows, 0);
        QVERIFY(d.columns.empty());
    }
}

void TstSqliteProvider::syntaxErrorPositionIsExact()
{
    auto connection = memory();
    QVERIFY(connection);
    const QString sql = QStringLiteral("SELECT 1 FROM FROM t");
    auto stream = connection->execute(Statement { sql, {}, 500 }, {});
    QVERIFY(!stream);
    const Error error = std::move(stream).error();
    QCOMPARE(error.kind, ErrorKind::Syntax);
    QCOMPARE(error.message, QStringLiteral("There is a syntax error near \"FROM\"."));
    QCOMPARE(error.position.value_or(-1), sql.indexOf(QLatin1String("FROM"), 14));
    QVERIFY(error.detail.contains(QLatin1String("SQLITE_ERROR")));
}

void TstSqliteProvider::syntaxErrorPositionIsInUtf16Units()
{
    auto connection = memory();
    QVERIFY(connection);
    const QString sql = QStringLiteral("SELECT '😀é' FROM FROM t"); // 4 + 2 bytes before 'FROM'
    auto stream = connection->execute(Statement { sql, {}, 500 }, {});
    QVERIFY(!stream);
    QCOMPARE(std::move(stream).error().position.value_or(-1),
             sql.lastIndexOf(QLatin1String("FROM t")));
}

void TstSqliteProvider::transactions()
{
    auto connection = memory();
    QVERIFY(connection);
    QVERIFY(drain(*connection, QStringLiteral("CREATE TABLE t(a)")).ok);
    QVERIFY(connection->begin());
    QVERIFY(drain(*connection, QStringLiteral("INSERT INTO t VALUES (1)")).ok);
    QVERIFY(connection->rollback());
    QCOMPARE(drain(*connection, QStringLiteral("SELECT * FROM t")).rows, 0);
    QVERIFY(connection->begin());
    QVERIFY(drain(*connection, QStringLiteral("INSERT INTO t VALUES (1)")).ok);
    QVERIFY(connection->commit());
    QCOMPARE(drain(*connection, QStringLiteral("SELECT * FROM t")).rows, 1);
    QVERIFY(!connection->commit()); // no transaction active
}

void TstSqliteProvider::dialectSpecifics()
{
    const ISqlDialect &dialect = m_provider->dialect();
    QCOMPARE(dialect.quoteIdentifier(u"a\"b"), QStringLiteral("\"a\"\"b\""));
    QCOMPARE(dialect.quoteLiteral(u"it's"), QStringLiteral("'it''s'"));
    QCOMPARE(dialect.highlightingDefinition(), QStringLiteral("SQL"));
    QCOMPARE(dialect.limitClause(10, 0), QStringLiteral("LIMIT 10"));
    QCOMPARE(dialect.limitClause(10, 20), QStringLiteral("LIMIT 10 OFFSET 20"));
    QCOMPARE(dialect.limitClause(-1, 20), QStringLiteral("LIMIT -1 OFFSET 20"));
    QCOMPARE(dialect.explainPrefix(ExplainMode::Plan), QStringLiteral("EXPLAIN QUERY PLAN "));
    QVERIFY(dialect.keywords().contains(QStringLiteral("SELECT")));
    QVERIFY(dialect.keywords().size() > 100);

    auto connection = memory();
    QVERIFY(connection);
    const auto plan
        = drain(*connection, dialect.explainPrefix(ExplainMode::Plan) + QStringLiteral("SELECT 1"));
    QVERIFY2(plan.ok, qPrintable(plan.errorText()));
    QVERIFY(plan.rows > 0);
}

void TstSqliteProvider::sessionStreamsMillionRowsWithoutBlockingMainThread()
{
    ConnectionConfig config;
    config.values.insert(QStringLiteral("file"), QStringLiteral(":memory:"));
    Session session(m_provider->createConnection(config, {}));

    int ticks = 0;
    qint64 maxGap = 0;
    QElapsedTimer sinceTick;
    sinceTick.start();
    QTimer ticker;
    ticker.setInterval(5);
    connect(&ticker, &QTimer::timeout, this, [&] {
        maxGap = std::max(maxGap, sinceTick.restart());
        ++ticks;
    });
    ticker.start();

    qint64 rows = 0;
    qsizetype maxBatch = 0;
    bool done = false;
    QEventLoop loop; // QTRY_* would sleep 10 ms per spin, which dominates 2000 round trips
    connect(&session, &Session::batchReady, this,
            [&](const std::shared_ptr<const ResultBatch> &batch) {
                rows += batch->rowCount;
                maxBatch = std::max(maxBatch, batch->rowCount);
            });
    connect(&session, &Session::finished, this, [&](const ExecutionStats &stats) {
        if (stats.atEnd) {
            done = true;
            loop.quit();
        } else
            session.fetchMore(500);
    });
    QSignalSpy failed(&session, &Session::failed);

    QElapsedTimer clock;
    clock.start();
    session.open();
    session.execute(Statement { QStringLiteral("WITH RECURSIVE s(n) AS (SELECT 1 UNION ALL SELECT "
                                               "n + 1 FROM s WHERE n < 1000000) "
                                               "SELECT n, 'row ' || n FROM s"),
                                {},
                                500 });
    connect(&session, &Session::failed, &loop, &QEventLoop::quit);
    QTimer::singleShot(50'000, &loop, &QEventLoop::quit);
    loop.exec();
    QVERIFY2(done, "streaming did not finish");
    QCOMPARE(failed.count(), 0);
    qInfo() << "1M rows through Session in" << clock.elapsed() << "ms," << ticks
            << "main-thread ticks, max gap" << maxGap << "ms";
    QCOMPARE(rows, 1'000'000);
    QCOMPARE(maxBatch, 500);
    QVERIFY2(ticks >= 5, "the main thread did not process events while streaming");
    QVERIFY2(maxGap < 250, qPrintable(QStringLiteral("main thread stalled for %1 ms").arg(maxGap)));
}

void TstSqliteProvider::sessionCancelsInfiniteQuery()
{
    ConnectionConfig config;
    config.values.insert(QStringLiteral("file"), QStringLiteral(":memory:"));
    Session session(m_provider->createConnection(config, {}));
    QSignalSpy started(&session, &Session::started);
    QSignalSpy cancelled(&session, &Session::cancelled);
    QSignalSpy failed(&session, &Session::failed);
    QSignalSpy finished(&session, &Session::finished);

    session.open();
    session.execute(Statement { kSlowSql, {}, 500 });
    QTRY_COMPARE(started.count(), 1);
    QTest::qWait(150);

    QElapsedTimer clock;
    clock.start();
    session.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1, 2000);
    QVERIFY2(clock.elapsed() < 200, qPrintable(QString::number(clock.elapsed())));
    QCOMPARE(failed.count(), 0);

    session.execute(Statement { QStringLiteral("SELECT 41 + 1"), {}, 500 });
    QTRY_COMPARE(finished.count(), 1);
    const auto stats = finished.at(0).at(0).value<ExecutionStats>();
    QCOMPARE(stats.rowsFetched, 1);
    QVERIFY(stats.atEnd);
}

QTEST_GUILESS_MAIN(TstSqliteProvider)
#include "tst_sqlite_provider.moc"
