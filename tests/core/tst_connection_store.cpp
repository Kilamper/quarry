#include <quarry/connection_store.h>

#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <sqlite3.h>

using namespace quarry;

namespace {

const QString kSecret = QStringLiteral("S3cr3t-Pa55w0rd-XYZZY");

ConnectionProfile makeProfile(const QString &name = QStringLiteral("Local"))
{
    ConnectionProfile p;
    p.name = name;
    p.providerId = QStringLiteral("postgres");
    p.values = { { QStringLiteral("host"), QStringLiteral("localhost") },
                 { QStringLiteral("port"), 5432 } };
    p.colorIndex = 3;
    p.production = true;
    p.passwordMode = PasswordMode::Saved;
    return p;
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

class TstConnectionStore : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void defaultPathEndsWithQuarryDb();
    void roundtrip();
    void createsMissingParentDirectory();
    void timestampsAreUtcAndUpdatedAtMoves();
    void validationErrors();
    void secretsAreNeverWritten();
    void touchAndRemove();
    void migrationIsIdempotentAcrossReopen();
    void futureVersionIsRefusedUntouched();
    void corruptFileFailsCleanly();
    void callsAreAsynchronous();
    void queuedWorkSurvivesDestruction();
};

void TstConnectionStore::defaultPathEndsWithQuarryDb()
{
    QVERIFY(ConnectionStore::defaultDatabasePath().endsWith(QLatin1String("/quarry.db")));
}

void TstConnectionStore::roundtrip()
{
    QTemporaryDir dir;
    ConnectionStore store(dir.filePath(QStringLiteral("quarry.db")));
    QSignalSpy loaded(&store, &ConnectionStore::loaded);
    QSignalSpy saved(&store, &ConnectionStore::saved);
    QSignalSpy removed(&store, &ConnectionStore::removed);
    QSignalSpy failed(&store, &ConnectionStore::failed);

    store.load();
    QTRY_COMPARE(loaded.count(), 1);
    QVERIFY(loaded.at(0).at(0).value<QList<ConnectionProfile>>().isEmpty());

    store.save(makeProfile());
    QTRY_COMPARE(saved.count(), 1);
    const auto first = saved.at(0).at(0).value<ConnectionProfile>();
    QVERIFY(!first.id.isNull());
    QVERIFY(first.createdAt.isValid());
    QCOMPARE(first.createdAt, first.updatedAt);
    QVERIFY(!first.lastUsedAt.has_value());
    QCOMPARE(first.values.value(QStringLiteral("host")).toString(), QStringLiteral("localhost"));
    QCOMPARE(first.values.value(QStringLiteral("port")).toInt(), 5432);
    QCOMPARE(first.colorIndex, 3);
    QVERIFY(first.production);
    QCOMPARE(first.passwordMode, PasswordMode::Saved);

    auto edited = first;
    edited.name = QStringLiteral("Renamed");
    edited.production = false;
    store.save(edited);
    QTRY_COMPARE(saved.count(), 2);
    const auto second = saved.at(1).at(0).value<ConnectionProfile>();
    QCOMPARE(second.id, first.id);
    QCOMPARE(second.name, QStringLiteral("Renamed"));
    QCOMPARE(second.createdAt, first.createdAt);

    store.load();
    QTRY_COMPARE(loaded.count(), 2);
    const auto all = loaded.at(1).at(0).value<QList<ConnectionProfile>>();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all.at(0).name, QStringLiteral("Renamed"));
    QVERIFY(!all.at(0).production);

    store.remove(first.id);
    QTRY_COMPARE(removed.count(), 1);
    QCOMPARE(removed.at(0).at(0).toUuid(), first.id);
    store.load();
    QTRY_COMPARE(loaded.count(), 3);
    QVERIFY(loaded.at(2).at(0).value<QList<ConnectionProfile>>().isEmpty());
    QCOMPARE(failed.count(), 0);
}

void TstConnectionStore::createsMissingParentDirectory()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("a/b/quarry.db"));
    ConnectionStore store(path);
    QSignalSpy loaded(&store, &ConnectionStore::loaded);
    store.load();
    QTRY_COMPARE(loaded.count(), 1);
    QVERIFY(QFileInfo::exists(path));
}

void TstConnectionStore::timestampsAreUtcAndUpdatedAtMoves()
{
    QTemporaryDir dir;
    ConnectionStore store(dir.filePath(QStringLiteral("quarry.db")));
    QSignalSpy saved(&store, &ConnectionStore::saved);
    const QDateTime before = QDateTime::currentDateTimeUtc().addMSecs(-5);
    store.save(makeProfile());
    QTRY_COMPARE(saved.count(), 1);
    const auto first = saved.at(0).at(0).value<ConnectionProfile>();
    QCOMPARE(first.createdAt.timeSpec(), Qt::UTC);
    QVERIFY(first.createdAt >= before);
    QVERIFY(first.createdAt <= QDateTime::currentDateTimeUtc().addMSecs(5));

    QTest::qWait(20);
    store.save(first);
    QTRY_COMPARE(saved.count(), 2);
    const auto second = saved.at(1).at(0).value<ConnectionProfile>();
    QCOMPARE(second.createdAt, first.createdAt);
    QVERIFY(second.updatedAt > first.updatedAt);
}

void TstConnectionStore::validationErrors()
{
    QTemporaryDir dir;
    ConnectionStore store(dir.filePath(QStringLiteral("quarry.db")));
    QSignalSpy saved(&store, &ConnectionStore::saved);
    QSignalSpy failed(&store, &ConnectionStore::failed);

    store.save(makeProfile(QStringLiteral("   ")));
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(failed.at(0).at(0).value<Error>().kind, ErrorKind::Constraint);

    auto noProvider = makeProfile();
    noProvider.providerId.clear();
    store.save(noProvider);
    QTRY_COMPARE(failed.count(), 2);
    QCOMPARE(failed.at(1).at(0).value<Error>().kind, ErrorKind::Constraint);
    QCOMPARE(saved.count(), 0);
}

void TstConnectionStore::secretsAreNeverWritten()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("quarry.db"));
    const QStringList suffixes { QString(), QStringLiteral("-wal"), QStringLiteral("-shm") };
    {
        ConnectionStore store(path);
        QSignalSpy saved(&store, &ConnectionStore::saved);
        QSignalSpy loaded(&store, &ConnectionStore::loaded);

        auto p = makeProfile();
        p.values.insert(QStringLiteral("password"), kSecret);
        p.values.insert(QStringLiteral("Password"), kSecret);
        p.values.insert(QStringLiteral("sshPassphrase"), kSecret);
        store.save(p, { QStringLiteral("sshPassphrase") });
        QTRY_COMPARE(saved.count(), 1);
        const auto stored = saved.at(0).at(0).value<ConnectionProfile>();
        QVERIFY(!stored.values.contains(QStringLiteral("password")));
        QVERIFY(!stored.values.contains(QStringLiteral("Password")));
        QVERIFY(!stored.values.contains(QStringLiteral("sshPassphrase")));
        QVERIFY(stored.values.contains(QStringLiteral("host")));

        store.load();
        QTRY_COMPARE(loaded.count(), 1);
        const auto all = loaded.at(0).at(0).value<QList<ConnectionProfile>>();
        QCOMPARE(all.size(), 1);
        QVERIFY(!all.at(0).values.contains(QStringLiteral("password")));
        QVERIFY(!all.at(0).values.contains(QStringLiteral("sshPassphrase")));

        // Scan while the store is still open: this is where -wal/-shm hold the data.
        QByteArray everything;
        for (const QString &suffix : suffixes) {
            const QByteArray bytes = readAll(path + suffix);
            QVERIFY2(!bytes.contains(kSecret.toUtf8()), qPrintable(suffix));
            everything += bytes;
        }
        // Sanity check of the scan itself: stored data must be visible to it.
        QVERIFY(everything.contains("localhost"));
    }
    for (const QString &suffix : suffixes)
        QVERIFY2(!readAll(path + suffix).contains(kSecret.toUtf8()), qPrintable(suffix));
}

void TstConnectionStore::touchAndRemove()
{
    QTemporaryDir dir;
    ConnectionStore store(dir.filePath(QStringLiteral("quarry.db")));
    QSignalSpy saved(&store, &ConnectionStore::saved);
    QSignalSpy failed(&store, &ConnectionStore::failed);
    QSignalSpy removed(&store, &ConnectionStore::removed);

    store.save(makeProfile());
    QTRY_COMPARE(saved.count(), 1);
    const auto p = saved.at(0).at(0).value<ConnectionProfile>();
    store.touch(p.id);
    QTRY_COMPARE(saved.count(), 2);
    const auto touched = saved.at(1).at(0).value<ConnectionProfile>();
    QVERIFY(touched.lastUsedAt.has_value());
    QCOMPARE(touched.updatedAt, p.updatedAt);

    store.touch(QUuid::createUuid());
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(failed.at(0).at(0).value<Error>().kind, ErrorKind::NotFound);

    store.remove(QUuid::createUuid()); // unknown id is not an error
    QTRY_COMPARE(removed.count(), 1);
}

void TstConnectionStore::migrationIsIdempotentAcrossReopen()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("quarry.db"));
    QUuid id;
    for (int round = 0; round < 3; ++round) {
        ConnectionStore store(path);
        QSignalSpy loaded(&store, &ConnectionStore::loaded);
        QSignalSpy saved(&store, &ConnectionStore::saved);
        QSignalSpy failed(&store, &ConnectionStore::failed);
        if (round == 0) {
            store.save(makeProfile());
            QTRY_COMPARE(saved.count(), 1);
            id = saved.at(0).at(0).value<ConnectionProfile>().id;
        } else {
            store.load();
            QTRY_COMPARE(loaded.count(), 1);
            const auto all = loaded.at(0).at(0).value<QList<ConnectionProfile>>();
            QCOMPARE(all.size(), 1);
            QCOMPARE(all.at(0).id, id);
        }
        QCOMPARE(failed.count(), 0);
    }
    sqlite3 *db = nullptr;
    QCOMPARE(sqlite3_open_v2(path.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr),
             SQLITE_OK);
    sqlite3_stmt *stmt = nullptr;
    QCOMPARE(sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &stmt, nullptr), SQLITE_OK);
    QCOMPARE(sqlite3_step(stmt), SQLITE_ROW);
    QCOMPARE(sqlite3_column_int(stmt, 0), 2);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

void TstConnectionStore::futureVersionIsRefusedUntouched()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("quarry.db"));
    sqlite3 *db = nullptr;
    QCOMPARE(sqlite3_open_v2(path.toUtf8().constData(), &db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr),
             SQLITE_OK);
    QCOMPARE(sqlite3_exec(db, "CREATE TABLE future(x); PRAGMA user_version=99", nullptr, nullptr,
                          nullptr),
             SQLITE_OK);
    sqlite3_close(db);
    const QByteArray before = readAll(path);
    QVERIFY(!before.isEmpty());

    {
        ConnectionStore store(path);
        QSignalSpy failed(&store, &ConnectionStore::failed);
        QSignalSpy saved(&store, &ConnectionStore::saved);
        store.load();
        store.save(makeProfile());
        QTRY_COMPARE(failed.count(), 2);
        const auto error = failed.at(0).at(0).value<Error>();
        QCOMPARE(error.message,
                 QStringLiteral("This database was created by a newer version of Quarry."));
        QCOMPARE(error.kind, ErrorKind::Unsupported);
        QCOMPARE(saved.count(), 0);
    }
    QCOMPARE(readAll(path), before);
}

void TstConnectionStore::corruptFileFailsCleanly()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("quarry.db"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(4096, 'x'));
    file.close();

    ConnectionStore store(path);
    QSignalSpy failed(&store, &ConnectionStore::failed);
    store.load();
    QTRY_COMPARE(failed.count(), 1);
    const auto error = failed.at(0).at(0).value<Error>();
    QVERIFY(!error.message.isEmpty());
    QVERIFY(!error.detail.isEmpty());
}

void TstConnectionStore::callsAreAsynchronous()
{
    QTemporaryDir dir;
    ConnectionStore store(dir.filePath(QStringLiteral("quarry.db")));
    QSignalSpy saved(&store, &ConnectionStore::saved);
    QSignalSpy loaded(&store, &ConnectionStore::loaded);

    // Nothing can be delivered until this thread runs its event loop.
    store.save(makeProfile());
    store.load();
    QCOMPARE(saved.count(), 0);
    QCOMPARE(loaded.count(), 0);
    QTRY_COMPARE(loaded.count(), 1);
    QCOMPARE(saved.count(), 1);
    // Operations run in call order: the load already sees the save.
    QCOMPARE(loaded.at(0).at(0).value<QList<ConnectionProfile>>().size(), 1);
}

void TstConnectionStore::queuedWorkSurvivesDestruction()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("quarry.db"));
    {
        ConnectionStore store(path);
        store.save(makeProfile());
    } // destroyed right away
    ConnectionStore again(path);
    QSignalSpy loaded(&again, &ConnectionStore::loaded);
    again.load();
    QTRY_COMPARE(loaded.count(), 1);
    QCOMPARE(loaded.at(0).at(0).value<QList<ConnectionProfile>>().size(), 1);
}

QTEST_MAIN(TstConnectionStore)
#include "tst_connection_store.moc"
