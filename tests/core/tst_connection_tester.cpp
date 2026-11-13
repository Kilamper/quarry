#include <quarry/connection_tester.h>
#include <quarry/provider_registry.h>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace quarry;

namespace {

ConnectionProfile sqliteProfile(const QString &file, bool readOnly = false)
{
    ConnectionProfile p;
    p.id = QUuid::createUuid();
    p.name = QStringLiteral("test");
    p.providerId = QStringLiteral("sqlite");
    p.values = { { QStringLiteral("file"), file }, { QStringLiteral("readOnly"), readOnly } };
    return p;
}

ConnectionProfile slowProfile(const QString &providerId = QStringLiteral("slow"))
{
    ConnectionProfile p;
    p.providerId = providerId;
    return p;
}

} // namespace

class TstConnectionTester : public QObject
{
    Q_OBJECT

private slots:
    void succeedsOnTempFile();
    void failsOnMissingReadOnlyFile();
    void unknownProviderFails();
    void cancelEmitsCancelled();
    void cancelWhenIdleIsSilent();
    void timeoutFails();
    void newTestReplacesPrevious();
};

void TstConnectionTester::succeedsOnTempFile()
{
    ProviderRegistry registry;
    registry.scan(QStringLiteral(QUARRY_PLUGIN_DIR));
    QTemporaryDir dir;
    ConnectionTester tester(registry);
    QSignalSpy ok(&tester, &ConnectionTester::succeeded);
    QSignalSpy bad(&tester, &ConnectionTester::failed);

    tester.test(sqliteProfile(dir.filePath(QStringLiteral("t.db"))), {});
    QCOMPARE(ok.count(), 0); // asynchronous
    QTRY_COMPARE_WITH_TIMEOUT(ok.count(), 1, 10000);
    QCOMPARE(ok.at(0).at(0).value<ServerInfo>().product, QStringLiteral("SQLite"));
    QCOMPARE(bad.count(), 0);

    // The tester is reusable.
    tester.test(sqliteProfile(dir.filePath(QStringLiteral("t.db"))), {});
    QTRY_COMPARE_WITH_TIMEOUT(ok.count(), 2, 10000);
}

void TstConnectionTester::failsOnMissingReadOnlyFile()
{
    ProviderRegistry registry;
    registry.scan(QStringLiteral(QUARRY_PLUGIN_DIR));
    QTemporaryDir dir;
    ConnectionTester tester(registry);
    QSignalSpy ok(&tester, &ConnectionTester::succeeded);
    QSignalSpy bad(&tester, &ConnectionTester::failed);

    tester.test(sqliteProfile(dir.filePath(QStringLiteral("missing.db")), true), {});
    QTRY_COMPARE_WITH_TIMEOUT(bad.count(), 1, 10000);
    const auto error = bad.at(0).at(0).value<Error>();
    QCOMPARE(error.kind, ErrorKind::NotFound);
    QVERIFY(!error.message.isEmpty());
    QVERIFY(error.detail.contains(QLatin1String("missing.db")));
    QCOMPARE(ok.count(), 0);
}

void TstConnectionTester::unknownProviderFails()
{
    ProviderRegistry registry;
    registry.scan(QStringLiteral(QUARRY_PLUGIN_DIR));
    ConnectionTester tester(registry);
    QSignalSpy bad(&tester, &ConnectionTester::failed);

    auto p = sqliteProfile(QStringLiteral(":memory:"));
    p.providerId = QStringLiteral("nope");
    tester.test(p, {});
    QCOMPARE(bad.count(), 0); // asynchronous, even for immediate errors
    QTRY_COMPARE(bad.count(), 1);
    const auto error = bad.at(0).at(0).value<Error>();
    QCOMPARE(error.kind, ErrorKind::NotFound);
    QVERIFY(!error.detail.isEmpty());
}

void TstConnectionTester::cancelEmitsCancelled()
{
    ProviderRegistry registry;
    registry.scan(QStringLiteral(QUARRY_SLOW_PLUGIN_DIR));
    ConnectionTester tester(registry);
    QSignalSpy ok(&tester, &ConnectionTester::succeeded);
    QSignalSpy bad(&tester, &ConnectionTester::failed);
    QSignalSpy cancelled(&tester, &ConnectionTester::cancelled);

    tester.test(slowProfile(), {});
    QTest::qWait(100); // let open() start blocking
    tester.cancel();
    QCOMPARE(cancelled.count(), 1);
    QTest::qWait(200);
    QCOMPARE(ok.count() + bad.count(), 0);
}

void TstConnectionTester::cancelWhenIdleIsSilent()
{
    ProviderRegistry registry;
    ConnectionTester tester(registry);
    QSignalSpy cancelled(&tester, &ConnectionTester::cancelled);
    tester.cancel();
    QCOMPARE(cancelled.count(), 0);
}

void TstConnectionTester::timeoutFails()
{
    ProviderRegistry registry;
    registry.scan(QStringLiteral(QUARRY_SLOW_PLUGIN_DIR));
    ConnectionTester tester(registry);
    tester.setTimeoutMs(300);
    QSignalSpy bad(&tester, &ConnectionTester::failed);

    tester.test(slowProfile(), {});
    QTRY_COMPARE_WITH_TIMEOUT(bad.count(), 1, 10000);
    const auto error = bad.at(0).at(0).value<Error>();
    QCOMPARE(error.kind, ErrorKind::Timeout);
    QCOMPARE(error.message, QStringLiteral("The server did not answer in 15 seconds."));
}

void TstConnectionTester::newTestReplacesPrevious()
{
    ProviderRegistry registry;
    registry.scan(QStringLiteral(QUARRY_SLOW_PLUGIN_DIR));
    ConnectionTester tester(registry);
    QSignalSpy ok(&tester, &ConnectionTester::succeeded);
    QSignalSpy bad(&tester, &ConnectionTester::failed);
    QSignalSpy cancelled(&tester, &ConnectionTester::cancelled);

    tester.test(slowProfile(), {});
    QTest::qWait(50);
    // The second test fails at once; the first one emits nothing.
    tester.test(slowProfile(QStringLiteral("nope")), {});
    QTRY_COMPARE(bad.count(), 1);
    QTest::qWait(100);
    QCOMPARE(bad.count(), 1);
    QCOMPARE(ok.count(), 0);
    QCOMPARE(cancelled.count(), 0);
}

QTEST_MAIN(TstConnectionTester)
#include "tst_connection_tester.moc"
