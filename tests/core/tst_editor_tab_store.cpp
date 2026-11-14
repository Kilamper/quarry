#include <quarry/connection_store.h>
#include <quarry/editor_tab_store.h>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <sqlite3.h>

using namespace quarry;

namespace {

EditorTab makeTab(const QString &title = QStringLiteral("Query 1"), int position = 0)
{
    EditorTab t;
    t.title = title;
    t.text = QStringLiteral("SELECT 1;");
    t.cursorPosition = 4;
    t.position = position;
    return t;
}

ConnectionProfile makeProfile()
{
    ConnectionProfile p;
    p.name = QStringLiteral("Local");
    p.providerId = QStringLiteral("sqlite");
    return p;
}

int userVersion(const QString &path)
{
    sqlite3 *db = nullptr;
    int version = -1;
    if (sqlite3_open_v2(path.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr)
        == SQLITE_OK) {
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &stmt, nullptr) == SQLITE_OK
            && sqlite3_step(stmt) == SQLITE_ROW)
            version = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return version;
}

// Saves through the store and returns the stored tab; fails the test via QTRY on timeout.
bool saveTab(EditorTabStore &store, QSignalSpy &saved, const EditorTab &tab, EditorTab &out)
{
    const qsizetype before = saved.count();
    store.save(tab);
    if (!QTest::qWaitFor([&] { return saved.count() > before; }))
        return false;
    out = saved.at(before).at(0).value<EditorTab>();
    return true;
}

} // namespace

class TstEditorTabStore : public QObject
{
    Q_OBJECT

private slots:
    void saveNewAssignsIdAndUpdatedAt();
    void updateKeepsIdAndMovesUpdatedAt();
    void loadOrdering();
    void removeAlsoUnknown();
    void emptyTitleFails();
    void nulloptConnectionRoundtrip();
    void largeTextRoundtrip();
    void destructorFlushesQueuedSaves();
    void deletingConnectionClearsTabConnection();
    void unknownConnectionFails();
    void upgradesVersion1DatabaseKeepingConnections();
    void concurrentFirstOpenOfBothStores();
};

void TstEditorTabStore::saveNewAssignsIdAndUpdatedAt()
{
    QTemporaryDir dir;
    EditorTabStore store(dir.filePath(QStringLiteral("quarry.db")));
    QSignalSpy saved(&store, &EditorTabStore::saved);
    const QDateTime before = QDateTime::currentDateTimeUtc().addMSecs(-5);
    EditorTab t;
    QVERIFY(saveTab(store, saved, makeTab(), t));
    QVERIFY(!t.id.isNull());
    QVERIFY(t.updatedAt.isValid());
    QCOMPARE(t.updatedAt.timeSpec(), Qt::UTC);
    QVERIFY(t.updatedAt >= before);
    QCOMPARE(t.title, QStringLiteral("Query 1"));
    QCOMPARE(t.cursorPosition, 4);
}

void TstEditorTabStore::updateKeepsIdAndMovesUpdatedAt()
{
    QTemporaryDir dir;
    EditorTabStore store(dir.filePath(QStringLiteral("quarry.db")));
    QSignalSpy saved(&store, &EditorTabStore::saved);
    QSignalSpy loaded(&store, &EditorTabStore::loaded);
    EditorTab first;
    QVERIFY(saveTab(store, saved, makeTab(), first));
    QTest::qWait(20);
    auto edited = first;
    edited.text = QStringLiteral("SELECT 2;");
    edited.cursorPosition = 9;
    EditorTab second;
    QVERIFY(saveTab(store, saved, edited, second));
    QCOMPARE(second.id, first.id);
    QVERIFY(second.updatedAt > first.updatedAt);

    store.load();
    QTRY_COMPARE(loaded.count(), 1);
    const auto all = loaded.at(0).at(0).value<QList<EditorTab>>();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all.at(0).text, QStringLiteral("SELECT 2;"));
    QCOMPARE(all.at(0).cursorPosition, 9);
    QCOMPARE(all.at(0).updatedAt, second.updatedAt);
}

void TstEditorTabStore::loadOrdering()
{
    QTemporaryDir dir;
    EditorTabStore store(dir.filePath(QStringLiteral("quarry.db")));
    QSignalSpy saved(&store, &EditorTabStore::saved);
    QSignalSpy loaded(&store, &EditorTabStore::loaded);
    EditorTab c, a, b1, b2;
    QVERIFY(saveTab(store, saved, makeTab(QStringLiteral("c"), 2), c));
    QVERIFY(saveTab(store, saved, makeTab(QStringLiteral("a"), 0), a));
    QTest::qWait(5);
    QVERIFY(saveTab(store, saved, makeTab(QStringLiteral("b1"), 1), b1));
    QTest::qWait(5);
    QVERIFY(saveTab(store, saved, makeTab(QStringLiteral("b2"), 1), b2));

    store.load();
    QTRY_COMPARE(loaded.count(), 1);
    const auto all = loaded.at(0).at(0).value<QList<EditorTab>>();
    QCOMPARE(all.size(), 4);
    // Equal position: older updatedAt first.
    QCOMPARE(all.at(0).title, QStringLiteral("a"));
    QCOMPARE(all.at(1).title, QStringLiteral("b1"));
    QCOMPARE(all.at(2).title, QStringLiteral("b2"));
    QCOMPARE(all.at(3).title, QStringLiteral("c"));
}

void TstEditorTabStore::removeAlsoUnknown()
{
    QTemporaryDir dir;
    EditorTabStore store(dir.filePath(QStringLiteral("quarry.db")));
    QSignalSpy saved(&store, &EditorTabStore::saved);
    QSignalSpy removed(&store, &EditorTabStore::removed);
    QSignalSpy loaded(&store, &EditorTabStore::loaded);
    QSignalSpy failed(&store, &EditorTabStore::failed);
    EditorTab t;
    QVERIFY(saveTab(store, saved, makeTab(), t));
    store.remove(t.id);
    QTRY_COMPARE(removed.count(), 1);
    QCOMPARE(removed.at(0).at(0).toUuid(), t.id);
    store.remove(QUuid::createUuid());
    QTRY_COMPARE(removed.count(), 2);
    store.load();
    QTRY_COMPARE(loaded.count(), 1);
    QVERIFY(loaded.at(0).at(0).value<QList<EditorTab>>().isEmpty());
    QCOMPARE(failed.count(), 0);
}

void TstEditorTabStore::emptyTitleFails()
{
    QTemporaryDir dir;
    EditorTabStore store(dir.filePath(QStringLiteral("quarry.db")));
    QSignalSpy saved(&store, &EditorTabStore::saved);
    QSignalSpy failed(&store, &EditorTabStore::failed);
    store.save(makeTab(QStringLiteral("  ")));
    QTRY_COMPARE(failed.count(), 1);
    const auto error = failed.at(0).at(0).value<Error>();
    QCOMPARE(error.kind, ErrorKind::Constraint);
    QVERIFY(!error.message.isEmpty());
    QCOMPARE(saved.count(), 0);
}

void TstEditorTabStore::nulloptConnectionRoundtrip()
{
    QTemporaryDir dir;
    EditorTabStore store(dir.filePath(QStringLiteral("quarry.db")));
    QSignalSpy saved(&store, &EditorTabStore::saved);
    QSignalSpy loaded(&store, &EditorTabStore::loaded);
    EditorTab t;
    QVERIFY(saveTab(store, saved, makeTab(), t));
    QVERIFY(!t.connectionId.has_value());
    store.load();
    QTRY_COMPARE(loaded.count(), 1);
    const auto all = loaded.at(0).at(0).value<QList<EditorTab>>();
    QCOMPARE(all.size(), 1);
    QVERIFY(!all.at(0).connectionId.has_value());
}

void TstEditorTabStore::largeTextRoundtrip()
{
    QTemporaryDir dir;
    EditorTabStore store(dir.filePath(QStringLiteral("quarry.db")));
    QSignalSpy saved(&store, &EditorTabStore::saved);
    QSignalSpy loaded(&store, &EditorTabStore::loaded);
    const QString chunk = QStringLiteral("SELECT 'é 日本 😀' FROM t;\n");
    const QString big = chunk.repeated(1100000 / static_cast<int>(chunk.toUtf8().size()) + 1);
    auto tab = makeTab();
    tab.text = big;
    EditorTab t;
    QVERIFY(saveTab(store, saved, tab, t));
    store.load();
    QTRY_COMPARE(loaded.count(), 1);
    const auto all = loaded.at(0).at(0).value<QList<EditorTab>>();
    QCOMPARE(all.size(), 1);
    QVERIFY(all.at(0).text == big);
}

void TstEditorTabStore::destructorFlushesQueuedSaves()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("quarry.db"));
    constexpr int kCount = 25;
    {
        EditorTabStore store(path);
        for (int i = 0; i < kCount; ++i)
            store.save(makeTab(QStringLiteral("Query %1").arg(i + 1), i));
    } // destroyed right away
    EditorTabStore again(path);
    QSignalSpy loaded(&again, &EditorTabStore::loaded);
    again.load();
    QTRY_COMPARE(loaded.count(), 1);
    const auto all = loaded.at(0).at(0).value<QList<EditorTab>>();
    QCOMPARE(all.size(), kCount);
    QCOMPARE(all.last().title, QStringLiteral("Query 25"));
}

void TstEditorTabStore::deletingConnectionClearsTabConnection()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("quarry.db"));
    ConnectionStore connections(path);
    EditorTabStore tabs(path);
    QSignalSpy connSaved(&connections, &ConnectionStore::saved);
    QSignalSpy connRemoved(&connections, &ConnectionStore::removed);
    QSignalSpy saved(&tabs, &EditorTabStore::saved);
    QSignalSpy loaded(&tabs, &EditorTabStore::loaded);
    QSignalSpy failed(&tabs, &EditorTabStore::failed);

    connections.save(makeProfile());
    QTRY_COMPARE(connSaved.count(), 1);
    const auto connection = connSaved.at(0).at(0).value<ConnectionProfile>();

    auto tab = makeTab();
    tab.connectionId = connection.id;
    EditorTab t;
    QVERIFY(saveTab(tabs, saved, tab, t));
    QCOMPARE(t.connectionId, std::optional<QUuid>(connection.id));
    tabs.load();
    QTRY_COMPARE(loaded.count(), 1);
    QCOMPARE(loaded.at(0).at(0).value<QList<EditorTab>>().at(0).connectionId,
             std::optional<QUuid>(connection.id));

    connections.remove(connection.id);
    QTRY_COMPARE(connRemoved.count(), 1);
    tabs.load();
    QTRY_COMPARE(loaded.count(), 2);
    const auto all = loaded.at(1).at(0).value<QList<EditorTab>>();
    QCOMPARE(all.size(), 1); // the tab survives
    QVERIFY(!all.at(0).connectionId.has_value());
    QCOMPARE(failed.count(), 0);
}

void TstEditorTabStore::unknownConnectionFails()
{
    QTemporaryDir dir;
    EditorTabStore store(dir.filePath(QStringLiteral("quarry.db")));
    QSignalSpy saved(&store, &EditorTabStore::saved);
    QSignalSpy failed(&store, &EditorTabStore::failed);
    QSignalSpy loaded(&store, &EditorTabStore::loaded);
    auto tab = makeTab();
    tab.connectionId = QUuid::createUuid();
    store.save(tab);
    QTRY_COMPARE(failed.count(), 1);
    const auto error = failed.at(0).at(0).value<Error>();
    QCOMPARE(error.kind, ErrorKind::Constraint);
    QVERIFY(!error.message.isEmpty());
    QVERIFY(!error.detail.isEmpty());
    QCOMPARE(saved.count(), 0);
    store.load();
    QTRY_COMPARE(loaded.count(), 1);
    QVERIFY(loaded.at(0).at(0).value<QList<EditorTab>>().isEmpty());
}

void TstEditorTabStore::upgradesVersion1DatabaseKeepingConnections()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("quarry.db"));
    const QUuid id = QUuid::createUuid();
    {
        // Migration 1 as released, written by hand.
        sqlite3 *db = nullptr;
        QCOMPARE(sqlite3_open_v2(path.toUtf8().constData(), &db,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr),
                 SQLITE_OK);
        const QByteArray sql = "CREATE TABLE connections(id TEXT PRIMARY KEY NOT NULL,"
                               " name TEXT NOT NULL, provider_id TEXT NOT NULL,"
                               " values_json TEXT NOT NULL DEFAULT '{}',"
                               " color_index INTEGER NOT NULL DEFAULT -1,"
                               " production INTEGER NOT NULL DEFAULT 0,"
                               " password_mode INTEGER NOT NULL DEFAULT 0,"
                               " created_at TEXT NOT NULL, updated_at TEXT NOT NULL,"
                               " last_used_at TEXT);"
                               "INSERT INTO connections(id, name, provider_id, created_at,"
                               " updated_at) VALUES('"
            + id.toString(QUuid::WithoutBraces).toUtf8()
            + "','Old','sqlite','2024-01-01T00:00:00.000Z','2024-01-01T00:00:00.000Z');"
              "PRAGMA user_version=1";
        QCOMPARE(sqlite3_exec(db, sql.constData(), nullptr, nullptr, nullptr), SQLITE_OK);
        sqlite3_close(db);
    }
    QCOMPARE(userVersion(path), 1);

    EditorTabStore tabs(path);
    QSignalSpy saved(&tabs, &EditorTabStore::saved);
    auto tab = makeTab();
    tab.connectionId = id; // the old connection is usable by the new table
    EditorTab t;
    QVERIFY(saveTab(tabs, saved, tab, t));
    QCOMPARE(userVersion(path), 2);

    ConnectionStore connections(path);
    QSignalSpy loaded(&connections, &ConnectionStore::loaded);
    connections.load();
    QTRY_COMPARE(loaded.count(), 1);
    const auto all = loaded.at(0).at(0).value<QList<ConnectionProfile>>();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all.at(0).id, id);
    QCOMPARE(all.at(0).name, QStringLiteral("Old"));
}

void TstEditorTabStore::concurrentFirstOpenOfBothStores()
{
    for (int round = 0; round < 20; ++round) {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("quarry.db"));
        {
            ConnectionStore connections(path);
            EditorTabStore tabs(path);
            QSignalSpy connLoaded(&connections, &ConnectionStore::loaded);
            QSignalSpy tabLoaded(&tabs, &EditorTabStore::loaded);
            QSignalSpy connFailed(&connections, &ConnectionStore::failed);
            QSignalSpy tabFailed(&tabs, &EditorTabStore::failed);
            connections.load();
            tabs.load();
            QTRY_VERIFY2(connLoaded.count() + connFailed.count() == 1
                             && tabLoaded.count() + tabFailed.count() == 1,
                         qPrintable(QStringLiteral("round %1").arg(round)));
            QVERIFY2(connFailed.count() == 0, qPrintable(QStringLiteral("round %1").arg(round)));
            QVERIFY2(tabFailed.count() == 0, qPrintable(QStringLiteral("round %1").arg(round)));
        }
        QCOMPARE(userVersion(path), 2);
    }
}

QTEST_MAIN(TstEditorTabStore)
#include "tst_editor_tab_store.moc"
