#include <quarry/credential_store.h>

#include <QSignalSpy>
#include <QtTest>

using namespace quarry;

class TstCredentialStore : public QObject
{
    Q_OBJECT

private slots:
    void sessionCacheSemantics();
    void keychainRoundtrip();
};

void TstCredentialStore::sessionCacheSemantics()
{
    CredentialStore store;
    const QUuid a = QUuid::createUuid();
    const QUuid b = QUuid::createUuid();
    QVERIFY(!store.sessionPassword(a).has_value());

    store.rememberForSession(a, QStringLiteral("one"));
    store.rememberForSession(b, QStringLiteral("two"));
    QCOMPARE(store.sessionPassword(a).value_or(QString()), QStringLiteral("one"));

    store.rememberForSession(a, QStringLiteral("three")); // overwrite
    QCOMPARE(store.sessionPassword(a).value_or(QString()), QStringLiteral("three"));
    QCOMPARE(store.sessionPassword(b).value_or(QString()), QStringLiteral("two"));

    store.rememberForSession(a, QString()); // an empty password is still a remembered answer
    QVERIFY(store.sessionPassword(a).has_value());

    store.forgetForSession(a);
    QVERIFY(!store.sessionPassword(a).has_value());
    QVERIFY(store.sessionPassword(b).has_value());
    store.forgetForSession(a); // idempotent
}

void TstCredentialStore::keychainRoundtrip()
{
    if (qEnvironmentVariable("QUARRY_TEST_KEYCHAIN") != QLatin1String("1"))
        QSKIP("set QUARRY_TEST_KEYCHAIN=1 to test the OS keychain");

    CredentialStore store;
    QSignalSpy read(&store, &CredentialStore::passwordRead);
    QSignalSpy written(&store, &CredentialStore::passwordWritten);
    QSignalSpy deleted(&store, &CredentialStore::passwordDeleted);
    QSignalSpy failed(&store, &CredentialStore::failed);

    const QUuid id = QUuid::createUuid(); // unique per run
    const QString password = QStringLiteral("päss wörd € 123");
    // Always clean up, even if a check below fails.
    const auto cleanup = qScopeGuard([&] {
        const qsizetype count = deleted.count();
        store.deletePassword(id);
        static_cast<void>(QTest::qWaitFor([&] { return deleted.count() > count; }, 10000));
    });

    store.writePassword(id, password);
    QTRY_COMPARE_WITH_TIMEOUT(written.count(), 1, 10000);
    QCOMPARE(failed.count(), 0);

    store.readPassword(id);
    QTRY_COMPARE_WITH_TIMEOUT(read.count(), 1, 10000);
    QCOMPARE(read.at(0).at(0).toUuid(), id);
    QCOMPARE(read.at(0).at(1).toString(), password);

    store.deletePassword(id);
    QTRY_COMPARE_WITH_TIMEOUT(deleted.count(), 1, 10000);

    store.readPassword(id);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 10000);
    QCOMPARE(failed.at(0).at(0).toUuid(), id);
    QCOMPARE(failed.at(0).at(1).value<Error>().kind, ErrorKind::NotFound);

    store.deletePassword(id); // deleting a missing entry counts as success
    QTRY_COMPARE_WITH_TIMEOUT(deleted.count(), 2, 10000);
}

QTEST_MAIN(TstCredentialStore)
#include "tst_credential_store.moc"
