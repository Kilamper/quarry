#include <quarry/provider_registry.h>
#include <quarry/session.h>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <sqlite3.h>

using namespace quarry;

namespace {

ConnectionProfile sqliteProfile(const QString &file)
{
    ConnectionProfile p;
    p.id = QUuid::createUuid();
    p.name = QStringLiteral("test");
    p.providerId = QStringLiteral("sqlite");
    p.values = { { QStringLiteral("file"), file } };
    return p;
}

// Creates `tables` tables (t0..tN-1) plus one view in a fresh file, with the C API directly.
bool createDatabase(const QString &file, int tables)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(file.toUtf8().constData(), &db) != SQLITE_OK)
        return false;
    QByteArray sql = "BEGIN;";
    for (int i = 0; i < tables; ++i)
        sql += "CREATE TABLE t" + QByteArray::number(i) + "(id INTEGER PRIMARY KEY, v TEXT);";
    sql += "CREATE VIEW v1 AS SELECT * FROM t0; COMMIT;";
    const bool ok = sqlite3_exec(db, sql.constData(), nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(db);
    return ok;
}

ObjectPath rootPath()
{
    return ObjectPath { {}, ObjectKind::Database };
}

// Registry must outlive the session: the connection's code lives in the plugin.
struct Fixture
{
    ProviderRegistry registry;
    QTemporaryDir dir;
    std::unique_ptr<Session> session;

    bool init(int tables)
    {
        registry.scan(QStringLiteral(QUARRY_PLUGIN_DIR));
        if (!dir.isValid() || !createDatabase(dir.filePath(QStringLiteral("t.db")), tables))
            return false;
        auto connection
            = registry.createConnection(sqliteProfile(dir.filePath(QStringLiteral("t.db"))), {});
        if (!connection)
            return false;
        session = std::make_unique<Session>(std::move(connection).value());
        return true;
    }
};

constexpr auto kSlowSql = "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x < "
                          "%1) SELECT count(*) FROM c";

Statement slowStatement(qint64 limit)
{
    return Statement { QString::fromLatin1(kSlowSql).arg(limit), {}, 500 };
}

} // namespace

class TstSchemaChildren : public QObject
{
    Q_OBJECT

private slots:
    void walksRootDatabaseAndFolder();
    void childrenBeforeOpenCompletesWaitsForIt();
    void childrenAfterFailedOpenFails();
    void childrenWithoutOpenFails();
    void queuedRequestsCompleteInOrder();
    void cancelFailsPendingAndSessionStaysUsable();
    void childrenDoesNotCancelRunningExecute();
    void closeFailsPendingChildren();
    void tenThousandTablesKeepMainThreadResponsive();
    void registryUnknownProvider();
    void registryCreatesSqliteConnection();
};

void TstSchemaChildren::walksRootDatabaseAndFolder()
{
    Fixture f;
    QVERIFY(f.init(3));
    QSignalSpy ready(f.session.get(), &Session::childrenReady);
    QSignalSpy failed(f.session.get(), &Session::childrenFailed);
    f.session->open();

    const quint64 id1 = f.session->children(rootPath());
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    QCOMPARE(ready.at(0).at(0).toULongLong(), id1);
    const auto dbs = ready.at(0).at(2).value<std::vector<SchemaNode>>();
    QCOMPARE(dbs.size(), size_t(1));
    QCOMPARE(dbs[0].name, QStringLiteral("main"));
    QCOMPARE(dbs[0].path.kind, ObjectKind::Database);

    const quint64 id2 = f.session->children(dbs[0].path);
    QTRY_COMPARE(ready.count(), 2);
    QVERIFY(id2 > id1);
    QCOMPARE(ready.at(1).at(0).toULongLong(), id2);
    const auto folders = ready.at(1).at(2).value<std::vector<SchemaNode>>();
    QCOMPARE(folders.size(), size_t(2));
    QCOMPARE(folders[0].name, QStringLiteral("Tables"));
    QCOMPARE(folders[0].path.kind, ObjectKind::Folder);

    const quint64 id3 = f.session->children(folders[0].path);
    QTRY_COMPARE(ready.count(), 3);
    QVERIFY(id3 > id2);
    QCOMPARE(ready.at(2).at(1).value<ObjectPath>().parts, folders[0].path.parts);
    const auto tables = ready.at(2).at(2).value<std::vector<SchemaNode>>();
    QCOMPARE(tables.size(), size_t(3));
    QCOMPARE(tables[0].name, QStringLiteral("t0"));
    QCOMPARE(tables[0].path.kind, ObjectKind::Table);
    QVERIFY(tables[0].hasChildren);
    QCOMPARE(failed.count(), 0);
}

void TstSchemaChildren::childrenBeforeOpenCompletesWaitsForIt()
{
    Fixture f;
    QVERIFY(f.init(1));
    QSignalSpy opened(f.session.get(), &Session::opened);
    QSignalSpy ready(f.session.get(), &Session::childrenReady);
    QSignalSpy failed(f.session.get(), &Session::childrenFailed);

    f.session->open();
    f.session->children(rootPath()); // queued right behind open()
    QCOMPARE(ready.count(), 0); // never synchronous
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    QCOMPARE(opened.count(), 1);
    QCOMPARE(failed.count(), 0);
}

void TstSchemaChildren::childrenAfterFailedOpenFails()
{
    Fixture f;
    QVERIFY(f.init(1));
    // A read-only open of a missing file fails.
    auto bad = sqliteProfile(f.dir.filePath(QStringLiteral("missing.db")));
    bad.values.insert(QStringLiteral("readOnly"), true);
    auto connection = f.registry.createConnection(bad, {});
    QVERIFY(connection.has_value());
    Session session(std::move(connection).value());
    QSignalSpy failedOpen(&session, &Session::failed);
    QSignalSpy ready(&session, &Session::childrenReady);
    QSignalSpy failed(&session, &Session::childrenFailed);

    session.open();
    const quint64 id = session.children(rootPath());
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 10000);
    QCOMPARE(failedOpen.count(), 1);
    QCOMPARE(failed.at(0).at(0).toULongLong(), id);
    QCOMPARE(failed.at(0).at(2).value<Error>().message,
             QStringLiteral("The connection is not open."));
    QCOMPARE(ready.count(), 0);
}

void TstSchemaChildren::childrenWithoutOpenFails()
{
    Fixture f;
    QVERIFY(f.init(1));
    QSignalSpy failed(f.session.get(), &Session::childrenFailed);
    f.session->children(rootPath());
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 10000);
    QCOMPARE(failed.at(0).at(2).value<Error>().message,
             QStringLiteral("The connection is not open."));
}

void TstSchemaChildren::queuedRequestsCompleteInOrder()
{
    Fixture f;
    QVERIFY(f.init(5));
    QSignalSpy ready(f.session.get(), &Session::childrenReady);
    f.session->open();
    const ObjectPath tablesFolder { { QStringLiteral("main"), QStringLiteral("Tables") },
                                    ObjectKind::Folder };
    const ObjectPath dbPath { { QStringLiteral("main") }, ObjectKind::Database };
    const std::vector<ObjectPath> parents { rootPath(), tablesFolder, dbPath, tablesFolder,
                                            rootPath() };
    std::vector<quint64> ids;
    ids.reserve(parents.size());
    for (const ObjectPath &p : parents)
        ids.push_back(f.session->children(p));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 5, 10000);
    for (size_t i = 0; i < ids.size(); ++i) {
        QCOMPARE(ready.at(int(i)).at(0).toULongLong(), ids[i]);
        QCOMPARE(ready.at(int(i)).at(1).value<ObjectPath>().parts, parents[i].parts);
        if (i > 0)
            QVERIFY(ids[i] > ids[i - 1]);
    }
    QVERIFY(ids.front() > 0);
}

void TstSchemaChildren::cancelFailsPendingAndSessionStaysUsable()
{
    Fixture f;
    QVERIFY(f.init(1));
    QSignalSpy ready(f.session.get(), &Session::childrenReady);
    QSignalSpy failed(f.session.get(), &Session::childrenFailed);
    QSignalSpy executeCancelled(f.session.get(), &Session::cancelled);
    f.session->open();
    // A long execute keeps the children requests pending on the worker.
    f.session->execute(slowStatement(2'000'000'000));
    std::vector<quint64> ids;
    ids.reserve(4);
    for (int i = 0; i < 4; ++i)
        ids.push_back(f.session->children(rootPath()));
    QTest::qWait(100);
    QCOMPARE(failed.count(), 0);

    f.session->cancel();
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 4, 10000);
    QTRY_COMPARE(executeCancelled.count(), 1);
    for (int i = 0; i < 4; ++i) {
        QCOMPARE(failed.at(i).at(0).toULongLong(), ids[size_t(i)]);
        QCOMPARE(failed.at(i).at(2).value<Error>().kind, ErrorKind::Cancelled);
    }
    QTest::qWait(50);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(failed.count(), 4);

    f.session->children(rootPath());
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    QCOMPARE(failed.count(), 4);
}

void TstSchemaChildren::childrenDoesNotCancelRunningExecute()
{
    Fixture f;
    QVERIFY(f.init(1));
    QSignalSpy finished(f.session.get(), &Session::finished);
    QSignalSpy executeCancelled(f.session.get(), &Session::cancelled);
    QSignalSpy executeFailed(f.session.get(), &Session::failed);
    QSignalSpy ready(f.session.get(), &Session::childrenReady);
    f.session->open();
    f.session->execute(slowStatement(3'000'000));
    f.session->children(rootPath());
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 30000);
    QCOMPARE(finished.count(), 1); // the execute ran to completion first (FIFO)
    QCOMPARE(executeCancelled.count(), 0);
    QCOMPARE(executeFailed.count(), 0);
}

void TstSchemaChildren::closeFailsPendingChildren()
{
    Fixture f;
    QVERIFY(f.init(1));
    QSignalSpy failed(f.session.get(), &Session::childrenFailed);
    QSignalSpy ready(f.session.get(), &Session::childrenReady);
    QSignalSpy closed(f.session.get(), &Session::closed);
    f.session->open();
    f.session->execute(slowStatement(2'000'000'000));
    for (int i = 0; i < 3; ++i)
        f.session->children(rootPath());
    QTest::qWait(100);
    f.session->close();
    QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 10000);
    QTRY_COMPARE(failed.count(), 3);
    QTest::qWait(50);
    QCOMPARE(failed.count(), 3);
    QCOMPARE(ready.count(), 0);
    for (int i = 0; i < 3; ++i)
        QCOMPARE(failed.at(i).at(2).value<Error>().kind, ErrorKind::Cancelled);
}

void TstSchemaChildren::tenThousandTablesKeepMainThreadResponsive()
{
    Fixture f;
    QVERIFY(f.init(10'000));
    QSignalSpy ready(f.session.get(), &Session::childrenReady);
    f.session->open();

    qint64 maxGap = 0;
    QElapsedTimer sinceTick;
    QTimer ticker;
    ticker.setInterval(5);
    connect(&ticker, &QTimer::timeout, this,
            [&] { maxGap = std::max(maxGap, sinceTick.restart()); });
    sinceTick.start();
    ticker.start();
    // A machine that stalls this long while idle (macOS CI runners) cannot measure a 50 ms budget.
    QTest::qWait(500);
    if (maxGap > 25)
        QSKIP(qPrintable(
            QStringLiteral("idle event-loop gap %1 ms: too noisy to measure").arg(maxGap)));
    maxGap = 0;

    QElapsedTimer clock;
    clock.start();
    f.session->children(
        ObjectPath { { QStringLiteral("main"), QStringLiteral("Tables") }, ObjectKind::Folder });
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 30000);
    const qint64 elapsed = clock.elapsed();
    ticker.stop();
    qInfo() << "children() of 10000 tables:" << elapsed << "ms, max event-loop gap" << maxGap
            << "ms";
    QCOMPARE(ready.at(0).at(2).value<std::vector<SchemaNode>>().size(), size_t(10'000));
    QVERIFY2(maxGap <= 50, qPrintable(QString::number(maxGap)));
}

void TstSchemaChildren::registryUnknownProvider()
{
    ProviderRegistry registry;
    registry.scan(QStringLiteral(QUARRY_PLUGIN_DIR));
    ConnectionProfile p;
    p.providerId = QStringLiteral("nope");
    auto result = registry.createConnection(p, {});
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().kind, ErrorKind::NotFound);
    QCOMPARE(result.error().message, QStringLiteral("This database engine is not available."));
    QVERIFY(!result.error().detail.isEmpty());
}

void TstSchemaChildren::registryCreatesSqliteConnection()
{
    ProviderRegistry registry;
    registry.scan(QStringLiteral(QUARRY_PLUGIN_DIR));
    auto result = registry.createConnection(sqliteProfile(QStringLiteral(":memory:")), {});
    QVERIFY(result.has_value());
    QVERIFY(result.value() != nullptr);
    QVERIFY(!result.value()->isOpen());
}

QTEST_GUILESS_MAIN(TstSchemaChildren)
#include "tst_schema_children.moc"
