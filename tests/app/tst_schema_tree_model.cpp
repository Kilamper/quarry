#include "ui/connection_controller.h"

#include <QAbstractItemModelTester>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <algorithm>
#include <memory>
#include <sqlite3.h>

using namespace quarry;
using namespace quarry::ui;

namespace {

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

} // namespace

class TstSchemaTreeModel : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    int m_files = 0;

    QString newFile(int tables)
    {
        const QString f = m_dir.filePath(QStringLiteral("data-%1.db").arg(++m_files));
        return createDatabase(f, tables) ? f : QString();
    }

    void startController(ConnectionController &c)
    {
        c.setDatabasePath(m_dir.filePath(QStringLiteral("quarry-%1.db").arg(++m_files)));
        c.setProviderDirectory(QStringLiteral(QUARRY_PLUGIN_DIR));
        c.start();
        QTRY_VERIFY_WITH_TIMEOUT(!c.model()->loading(), 10000);
    }

    QString addConnection(ConnectionController &c, const QString &name, const QString &file,
                          const QString &id = {})
    {
        c.prepare(id);
        c.form()->setValue(0, file);
        QSignalSpy saved(&c, &ConnectionController::connectionSaved);
        const bool ok = c.save({ { QStringLiteral("id"), id },
                                 { QStringLiteral("providerId"), QStringLiteral("sqlite") },
                                 { QStringLiteral("name"), name },
                                 { QStringLiteral("colorIndex"), 1 },
                                 { QStringLiteral("production"), false },
                                 { QStringLiteral("passwordMode"), 1 } });
        if (!ok)
            return {};
        saved.wait(10000);
        return saved.isEmpty() ? QString() : saved.at(0).at(0).toString();
    }

    static QString text(const QAbstractItemModel *m, const QModelIndex &i, int role)
    {
        return m->data(i, role).toString();
    }

    // Child of `parent` with this name (waits for it to appear).
    QModelIndex child(SchemaTreeModel *m, const QModelIndex &parent, const QString &name)
    {
        QModelIndex found;
        const auto lookup = [&] {
            const int count = m->rowCount(parent);
            for (int r = 0; r < count; ++r) {
                const QModelIndex i = m->index(r, 0, parent);
                if (text(m, i, SchemaTreeModel::NameRole) == name) {
                    found = i;
                    return true;
                }
            }
            return false;
        };
        [&] { QTRY_VERIFY_WITH_TIMEOUT(lookup(), 10000); }();
        return found;
    }

    // Expands (fetchMore) and waits until the real children are there.
    void expand(SchemaTreeModel *m, const QModelIndex &i)
    {
        QVERIFY(m->canFetchMore(i));
        m->fetchMore(i);
        QTRY_COMPARE_WITH_TIMEOUT(m->data(i, SchemaTreeModel::LoadStateRole).toInt(),
                                  int(SchemaTreeModel::Loaded), 20000);
    }

    // QAbstractItemModelTester follows canFetchMore()/fetchMore() everywhere, so with it attached
    // every node loads by itself. The tests that assert laziness run without it; these two run
    // with it, through every kind of change (roots, loading, error, retry, disconnect).
    static std::unique_ptr<QAbstractItemModelTester> attachTester(SchemaTreeModel *m)
    {
        return std::make_unique<QAbstractItemModelTester>(
            m, QAbstractItemModelTester::FailureReportingMode::Fatal);
    }

    // Saves a profile straight into the store, with the password mode the SQLite form would not
    // allow (the SQLite engine has no secret field). The password itself is never stored.
    QString addProfile(ConnectionController &c, const QString &name, const QString &file,
                       PasswordMode mode)
    {
        ConnectionProfile p;
        p.id = QUuid::createUuid();
        p.name = name;
        p.providerId = QStringLiteral("sqlite");
        p.values = { { QStringLiteral("file"), file } };
        p.passwordMode = mode;
        c.model()->store()->save(p);
        const QString id = p.id.toString(QUuid::WithoutBraces);
        (void)QTest::qWaitFor([&] { return c.model()->indexOf(id) >= 0; }, 10000);
        return id;
    }

private slots:
    void askEveryTimePromptsOnceThenRemembers();
    void askEveryTimeDismissedLeavesItCollapsed();
    void savedPasswordComesFromTheKeychainBackend();
    void testerSeesRootsAddedRenamedRemovedWhileEverythingLoads();
    void testerSeesLoadingErrorRetryAndDisconnect();
    void initialRoots();
    void expandConnectionShowsLoadingThenRows();
    void walksDatabaseFolderTableColumns();
    void brokenFileShowsErrorRowAndRetryRecovers();
    void failedOpenShowsErrorRow();
    void disconnectResetsTheSubtree();
    void connectionsAddRemoveRenameWhileExpanded();
    void staleReplyAfterDisconnectIsIgnored();
    void removedConnectionWithRequestInFlight();
    void tenThousandTablesDoNotBlockTheEventLoop();
};

void TstSchemaTreeModel::testerSeesRootsAddedRenamedRemovedWhileEverythingLoads()
{
    ConnectionController c;
    SchemaTreeModel *m = c.tree();
    const auto tester = attachTester(m);
    startController(c);
    const QString idA = addConnection(c, QStringLiteral("Alpha"), newFile(3));
    addConnection(c, QStringLiteral("Bravo"), newFile(2));
    QTRY_COMPARE_WITH_TIMEOUT(m->connectedIds().size(), 2, 20000); // the tester expanded both
    QTRY_COMPARE_WITH_TIMEOUT(m->data(m->index(0, 0), SchemaTreeModel::LoadStateRole).toInt(),
                              int(SchemaTreeModel::Loaded), 20000);
    // Rename in place, rename that moves the row, add before, delete the connected one.
    const QString fileA = m_dir.filePath(QStringLiteral("data-1.db"));
    addConnection(c, QStringLiteral("Alpine"), fileA, idA);
    addConnection(c, QStringLiteral("Zulu"), fileA, idA);
    QTRY_COMPARE(text(m, m->index(1, 0), SchemaTreeModel::NameRole), QStringLiteral("Zulu"));
    addConnection(c, QStringLiteral("Aardvark"), newFile(1));
    QTRY_COMPARE(m->rowCount(), 3);
    c.remove(idA);
    QTRY_COMPARE(m->rowCount(), 2);
    QTest::qWait(200); // let the tester walk what is left
    QCOMPARE(text(m, m->index(0, 0), SchemaTreeModel::NameRole), QStringLiteral("Aardvark"));
}

void TstSchemaTreeModel::testerSeesLoadingErrorRetryAndDisconnect()
{
    ConnectionController c;
    SchemaTreeModel *m = c.tree();
    const auto tester = attachTester(m);
    startController(c);
    const QString broken = m_dir.filePath(QStringLiteral("broken-t.db"));
    QVERIFY(QDir().mkdir(broken));
    const QString idBad = addConnection(c, QStringLiteral("Bad"), broken);
    const QString idGood = addConnection(c, QStringLiteral("Good"), newFile(4));
    QTRY_COMPARE_WITH_TIMEOUT(m->data(m->index(0, 0), SchemaTreeModel::LoadStateRole).toInt(),
                              int(SchemaTreeModel::Failed), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(m->isConnected(idGood), 20000);
    // Retry the broken one after fixing it; disconnect the good one (the tester reconnects it).
    QVERIFY(QDir().rmdir(broken));
    QVERIFY(createDatabase(broken, 1));
    m->retry(m->index(0, 0, m->index(0, 0)));
    QTRY_VERIFY_WITH_TIMEOUT(m->isConnected(idBad), 20000);
    m->disconnectConnection(idGood);
    m->disconnectConnection(idBad);
    QTRY_VERIFY_WITH_TIMEOUT(m->isConnected(idGood), 20000);
    QTest::qWait(200);
}

void TstSchemaTreeModel::askEveryTimePromptsOnceThenRemembers()
{
    ConnectionController c;
    SchemaTreeModel *m = c.tree();
    startController(c);
    const QString id = addProfile(c, QStringLiteral("Ask"), newFile(1), PasswordMode::AskEveryTime);
    const QModelIndex root = m->index(0, 0);
    QSignalSpy prompt(m, &SchemaTreeModel::passwordRequested);
    m->fetchMore(root);
    QTRY_COMPARE(prompt.count(), 1);
    QCOMPARE(prompt.at(0).at(0).toString(), id);
    QCOMPARE(prompt.at(0).at(1).toString(), QStringLiteral("Ask"));
    QCOMPARE(prompt.at(0).at(2).toBool(), false);
    QCOMPARE(m->data(root, SchemaTreeModel::LoadStateRole).toInt(), int(SchemaTreeModel::Loading));
    QVERIFY(!m->isConnected(id)); // nothing opens before the password is given
    m->providePassword(id, QStringLiteral("hunter2"));
    QTRY_COMPARE_WITH_TIMEOUT(m->data(root, SchemaTreeModel::LoadStateRole).toInt(),
                              int(SchemaTreeModel::Loaded), 10000);
    QVERIFY(m->isConnected(id));
    // Remembered for this run: the next connect does not ask again.
    m->disconnectConnection(id);
    expand(m, root);
    QCOMPARE(prompt.count(), 1);
    QVERIFY(m->isConnected(id));
}

void TstSchemaTreeModel::askEveryTimeDismissedLeavesItCollapsed()
{
    ConnectionController c;
    SchemaTreeModel *m = c.tree();
    startController(c);
    const QString id = addProfile(c, QStringLiteral("Ask"), newFile(1), PasswordMode::AskEveryTime);
    const QModelIndex root = m->index(0, 0);
    QSignalSpy prompt(m, &SchemaTreeModel::passwordRequested);
    QSignalSpy collapse(m, &SchemaTreeModel::collapseRequested);
    m->fetchMore(root);
    QTRY_COMPARE(prompt.count(), 1);
    m->cancelPassword(id);
    QCOMPARE(collapse.count(), 1);
    QCOMPARE(m->rowCount(root), 0);
    QCOMPARE(m->data(root, SchemaTreeModel::LoadStateRole).toInt(), int(SchemaTreeModel::Idle));
    QVERIFY(m->canFetchMore(root)); // expanding again asks again
    m->fetchMore(root);
    QTRY_COMPARE(prompt.count(), 2);
    m->cancelPassword(id);
    // An answer nobody is waiting for is ignored.
    m->providePassword(id, QStringLiteral("late"));
    QTest::qWait(50);
    QVERIFY(!m->isConnected(id));
}

void TstSchemaTreeModel::savedPasswordComesFromTheKeychainBackend()
{
    ConnectionController c;
    SchemaTreeModel *m = c.tree();
    // A fake keychain: no real CredentialStore call is ever made.
    bool fail = false;
    QStringList reads;
    CredentialBackend b;
    b.write = [](QUuid, const QString &) {};
    b.remove = [](QUuid) {};
    b.forget = [](QUuid) {};
    b.read = [&](QUuid id) {
        reads << id.toString(QUuid::WithoutBraces);
        QMetaObject::invokeMethod(
            &c,
            [&c, id, &fail] {
                if (fail)
                    c.onCredentialFailed(
                        id,
                        Error { ErrorKind::Unsupported,
                                QStringLiteral("The system keychain is not available."),
                                QStringLiteral("no backend"),
                                {},
                                {} });
                else
                    c.onPasswordRead(id, QStringLiteral("from-keychain"));
            },
            Qt::QueuedConnection);
    };
    c.setCredentialBackend(b);
    startController(c);
    const QString id = addProfile(c, QStringLiteral("Saved"), newFile(1), PasswordMode::Saved);
    const QModelIndex root = m->index(0, 0);

    fail = true;
    m->fetchMore(root);
    QTRY_COMPARE_WITH_TIMEOUT(m->data(root, SchemaTreeModel::LoadStateRole).toInt(),
                              int(SchemaTreeModel::Failed), 10000);
    QCOMPARE(reads, QStringList { id });
    const QModelIndex err = m->index(0, 0, root);
    QCOMPARE(text(m, err, SchemaTreeModel::KindRole), QStringLiteral("error"));
    QCOMPARE(text(m, err, SchemaTreeModel::NameRole),
             QStringLiteral("The system keychain is not available."));
    QCOMPARE(text(m, err, SchemaTreeModel::DetailRole), QStringLiteral("no backend"));

    fail = false;
    m->retry(err);
    QTRY_COMPARE_WITH_TIMEOUT(m->data(root, SchemaTreeModel::LoadStateRole).toInt(),
                              int(SchemaTreeModel::Loaded), 10000);
    QCOMPARE(reads.size(), 2);
    QVERIFY(m->isConnected(id));
}

void TstSchemaTreeModel::initialRoots()
{
    ConnectionController c;
    SchemaTreeModel *m = c.tree();
    startController(c);
    QCOMPARE(m->rowCount(), 0);
    QVERIFY(!m->hasChildren());

    addConnection(c, QStringLiteral("Bravo"), newFile(1));
    addConnection(c, QStringLiteral("Alpha"), newFile(1));
    QTRY_COMPARE(m->rowCount(), 2);
    const QModelIndex a = m->index(0, 0);
    const QModelIndex b = m->index(1, 0);
    QCOMPARE(text(m, a, SchemaTreeModel::NameRole), QStringLiteral("Alpha"));
    QCOMPARE(text(m, b, SchemaTreeModel::NameRole), QStringLiteral("Bravo"));
    QCOMPARE(text(m, a, SchemaTreeModel::KindRole), QStringLiteral("connection"));
    QCOMPARE(m->data(a, SchemaTreeModel::PositionRole).toInt(), 1);
    QCOMPARE(m->data(b, SchemaTreeModel::PositionRole).toInt(), 2);
    QCOMPARE(m->data(b, SchemaTreeModel::SetSizeRole).toInt(), 2);
    QCOMPARE(m->data(a, SchemaTreeModel::ColorIndexRole).toInt(), 1);
    QVERIFY(m->hasChildren(a));
    QVERIFY(m->canFetchMore(a));
    QCOMPARE(m->rowCount(a), 0); // nothing loaded, nothing connected
    QVERIFY(m->connectedIds().isEmpty());
    // Inserting a connection before the others renumbers them.
    addConnection(c, QStringLiteral("Aardvark"), newFile(1));
    QTRY_COMPARE(m->rowCount(), 3);
    QCOMPARE(m->data(m->index(2, 0), SchemaTreeModel::PositionRole).toInt(), 3);
    QCOMPARE(m->data(m->index(2, 0), SchemaTreeModel::SetSizeRole).toInt(), 3);
}

void TstSchemaTreeModel::expandConnectionShowsLoadingThenRows()
{
    ConnectionController c;
    SchemaTreeModel *m = c.tree();
    startController(c);
    addConnection(c, QStringLiteral("One"), newFile(3));
    QTRY_COMPARE(m->rowCount(), 1);
    const QModelIndex root = m->index(0, 0);
    QSignalSpy announced(m, &SchemaTreeModel::announcement);

    // The "Loading…" row exists only for a moment: record what is inserted under the root.
    QStringList inserted;
    connect(m, &QAbstractItemModel::rowsInserted, this,
            [&](const QModelIndex &parent, int first, int) {
                if (parent == root)
                    inserted << text(m, m->index(first, 0, parent), SchemaTreeModel::KindRole)
                            + QLatin1Char(':')
                            + text(m, m->index(first, 0, parent), SchemaTreeModel::NameRole);
            });
    m->fetchMore(root);
    QVERIFY(!m->canFetchMore(root)); // a second expand does not start a second request
    QTRY_VERIFY_WITH_TIMEOUT(!inserted.isEmpty(), 10000);
    QCOMPARE(inserted.first(), QStringLiteral("loading:Loading…")); // one synthetic row first
    QVERIFY(!announced.isEmpty());
    QCOMPARE(announced.at(0).at(0).toString(), QStringLiteral("Loading…"));

    QTRY_COMPARE_WITH_TIMEOUT(m->data(root, SchemaTreeModel::LoadStateRole).toInt(),
                              int(SchemaTreeModel::Loaded), 10000);
    QCOMPARE(m->rowCount(root), 1);
    const QModelIndex main = m->index(0, 0, root);
    QCOMPARE(text(m, main, SchemaTreeModel::NameRole), QStringLiteral("main"));
    QCOMPARE(text(m, main, SchemaTreeModel::KindRole), QStringLiteral("database"));
    QVERIFY(m->data(root, SchemaTreeModel::ConnectedRole).toBool());
    QCOMPARE(m->connectedIds().size(), 1);
    QVERIFY(m->isConnected(text(m, root, SchemaTreeModel::ConnectionIdRole)));
    QCOMPARE(m->connectionOf(main), root);
}

void TstSchemaTreeModel::walksDatabaseFolderTableColumns()
{
    ConnectionController c;
    SchemaTreeModel *m = c.tree();
    startController(c);
    addConnection(c, QStringLiteral("Walk"), newFile(3));
    QTRY_COMPARE(m->rowCount(), 1);
    const QModelIndex root = m->index(0, 0);
    expand(m, root);
    const QModelIndex main = m->index(0, 0, root);
    expand(m, main);
    QCOMPARE(m->rowCount(main), 2);
    const QModelIndex tables = child(m, main, QStringLiteral("Tables"));
    QCOMPARE(text(m, tables, SchemaTreeModel::KindRole), QStringLiteral("folder"));
    QCOMPARE(m->data(tables, SchemaTreeModel::SetSizeRole).toInt(), 2);
    expand(m, tables);
    QCOMPARE(m->rowCount(tables), 3);
    const QModelIndex t0 = child(m, tables, QStringLiteral("t0"));
    QCOMPARE(text(m, t0, SchemaTreeModel::KindRole), QStringLiteral("table"));
    QCOMPARE(m->data(t0, SchemaTreeModel::PositionRole).toInt(), 1);
    QCOMPARE(m->data(t0, SchemaTreeModel::SetSizeRole).toInt(), 3);
    QCOMPARE(m->parent(t0), tables);
    QCOMPARE(m->connectionIdOf(t0), m->connectionIdOf(root));
    expand(m, t0);
    QCOMPARE(m->rowCount(t0), 3); // Columns, Indexes, Foreign keys
    const QModelIndex columns = child(m, t0, QStringLiteral("Columns"));
    expand(m, columns);
    QCOMPARE(m->rowCount(columns), 2); // id, v
    const QModelIndex id = m->index(0, 0, columns);
    QCOMPARE(text(m, id, SchemaTreeModel::KindRole), QStringLiteral("column"));
    QVERIFY(!m->hasChildren(id)); // leaves have no expander
    QVERIFY(!m->canFetchMore(id));
    QVERIFY(!text(m, id, SchemaTreeModel::DetailRole).isEmpty());
}

void TstSchemaTreeModel::brokenFileShowsErrorRowAndRetryRecovers()
{
    ConnectionController c;
    SchemaTreeModel *m = c.tree();
    startController(c);
    // A directory where the file should be: the open itself fails (until the directory goes).
    const QString file = m_dir.filePath(QStringLiteral("broken.db"));
    QVERIFY(QDir().mkdir(file));
    addConnection(c, QStringLiteral("Broken"), file);
    QTRY_COMPARE(m->rowCount(), 1);
    const QModelIndex root = m->index(0, 0);
    QSignalSpy announced(m, &SchemaTreeModel::announcement);
    m->fetchMore(root);
    QTRY_COMPARE_WITH_TIMEOUT(m->data(root, SchemaTreeModel::LoadStateRole).toInt(),
                              int(SchemaTreeModel::Failed), 10000);
    QCOMPARE(m->rowCount(root), 1);
    const QModelIndex err = m->index(0, 0, root);
    QCOMPARE(text(m, err, SchemaTreeModel::KindRole), QStringLiteral("error"));
    QVERIFY(!text(m, err, SchemaTreeModel::NameRole).isEmpty());
    QVERIFY(!text(m, err, SchemaTreeModel::DetailRole).isEmpty()); // technical text for the details
    QVERIFY(!m->hasChildren(err));
    QVERIFY(!m->data(root, SchemaTreeModel::ConnectedRole).toBool());
    QVERIFY(!m->canFetchMore(root)); // only retry() starts it again
    QCOMPARE(announced.last().at(1).toBool(), true); // errors are assertive

    // Fix the file, then Retry from the error row.
    QVERIFY(QDir().rmdir(file));
    QVERIFY(createDatabase(file, 2));
    m->retry(err);
    QVERIFY(!m->canFetchMore(root));
    QTRY_COMPARE_WITH_TIMEOUT(m->data(root, SchemaTreeModel::LoadStateRole).toInt(),
                              int(SchemaTreeModel::Loaded), 10000);
    QCOMPARE(text(m, m->index(0, 0, root), SchemaTreeModel::NameRole), QStringLiteral("main"));
    QVERIFY(m->data(root, SchemaTreeModel::ConnectedRole).toBool());
}

void TstSchemaTreeModel::failedOpenShowsErrorRow()
{
    ConnectionController c;
    SchemaTreeModel *m = c.tree();
    startController(c);
    // A directory is not a database file: the open itself fails.
    addConnection(c, QStringLiteral("Dir"), m_dir.path());
    QTRY_COMPARE(m->rowCount(), 1);
    const QModelIndex root = m->index(0, 0);
    m->fetchMore(root);
    QTRY_COMPARE_WITH_TIMEOUT(m->data(root, SchemaTreeModel::LoadStateRole).toInt(),
                              int(SchemaTreeModel::Failed), 10000);
    const QModelIndex err = m->index(0, 0, root);
    QCOMPARE(text(m, err, SchemaTreeModel::KindRole), QStringLiteral("error"));
    // The open error, not the generic "The connection is not open." of the queued request.
    QVERIFY2(text(m, err, SchemaTreeModel::NameRole)
                 != QStringLiteral("The connection is not open."),
             qPrintable(text(m, err, SchemaTreeModel::NameRole)));
    QVERIFY(!text(m, err, SchemaTreeModel::DetailRole).isEmpty());
    QTest::qWait(100); // the generic answer arrives later and must not replace it
    QCOMPARE(m->rowCount(root), 1);
    QCOMPARE(m->data(root, SchemaTreeModel::LoadStateRole).toInt(), int(SchemaTreeModel::Failed));
}

void TstSchemaTreeModel::disconnectResetsTheSubtree()
{
    ConnectionController c;
    SchemaTreeModel *m = c.tree();
    startController(c);
    addConnection(c, QStringLiteral("Disc"), newFile(2));
    QTRY_COMPARE(m->rowCount(), 1);
    const QModelIndex root = m->index(0, 0);
    expand(m, root);
    expand(m, m->index(0, 0, root));
    QSignalSpy collapse(m, &SchemaTreeModel::collapseRequested);
    QSignalSpy connectedSpy(m, &SchemaTreeModel::connectedChanged);

    m->disconnectConnection(text(m, root, SchemaTreeModel::ConnectionIdRole));
    QCOMPARE(collapse.count(), 1);
    QCOMPARE(collapse.at(0).at(0).toModelIndex(), root);
    QCOMPARE(m->rowCount(root), 0);
    QVERIFY(m->hasChildren(root)); // the expander stays
    QVERIFY(m->canFetchMore(root));
    QVERIFY(!m->data(root, SchemaTreeModel::ConnectedRole).toBool());
    QVERIFY(m->connectedIds().isEmpty());
    QVERIFY(connectedSpy.count() >= 1);

    // And it connects again.
    expand(m, root);
    QCOMPARE(m->rowCount(root), 1);
    QVERIFY(m->data(root, SchemaTreeModel::ConnectedRole).toBool());
}

void TstSchemaTreeModel::connectionsAddRemoveRenameWhileExpanded()
{
    ConnectionController c;
    SchemaTreeModel *m = c.tree();
    startController(c);
    const QString idA = addConnection(c, QStringLiteral("Alpha"), newFile(2));
    addConnection(c, QStringLiteral("Bravo"), newFile(2));
    QTRY_COMPARE(m->rowCount(), 2);
    expand(m, m->index(0, 0));
    QVERIFY(m->isConnected(idA));

    // Rename without moving: same row, still connected, children kept.
    addConnection(c, QStringLiteral("Alpine"), m_dir.filePath(QStringLiteral("data-1.db")), idA);
    QTRY_COMPARE(text(m, m->index(0, 0), SchemaTreeModel::NameRole), QStringLiteral("Alpine"));
    QVERIFY(m->isConnected(idA));
    QCOMPARE(m->rowCount(m->index(0, 0)), 1);

    // Rename that moves the row (Alpha -> Zulu): the open session and its subtree follow it.
    addConnection(c, QStringLiteral("Zulu"), m_dir.filePath(QStringLiteral("data-1.db")), idA);
    QTRY_COMPARE(text(m, m->index(1, 0), SchemaTreeModel::NameRole), QStringLiteral("Zulu"));
    QTest::qWait(50); // the parked node would be closed here if it had not been reclaimed
    QVERIFY(m->isConnected(idA));
    QCOMPARE(m->rowCount(m->index(1, 0)), 1);
    QCOMPARE(m->data(m->index(1, 0), SchemaTreeModel::PositionRole).toInt(), 2);
    QCOMPARE(m->data(m->index(0, 0), SchemaTreeModel::PositionRole).toInt(), 1);

    // Delete the connected one: its session is closed and the row is gone.
    c.remove(idA);
    QTRY_COMPARE(m->rowCount(), 1);
    QTRY_VERIFY(m->connectedIds().isEmpty());
    QCOMPARE(text(m, m->index(0, 0), SchemaTreeModel::NameRole), QStringLiteral("Bravo"));
    QCOMPARE(m->data(m->index(0, 0), SchemaTreeModel::SetSizeRole).toInt(), 1);
}

void TstSchemaTreeModel::staleReplyAfterDisconnectIsIgnored()
{
    ConnectionController c;
    SchemaTreeModel *m = c.tree();
    startController(c);
    addConnection(c, QStringLiteral("Stale"), newFile(5));
    QTRY_COMPARE(m->rowCount(), 1);
    const QModelIndex root = m->index(0, 0);
    const QString id = text(m, root, SchemaTreeModel::ConnectionIdRole);

    // Disconnect while the connection request is in flight.
    m->fetchMore(root);
    m->disconnectConnection(id);
    QTest::qWait(300);
    QCOMPARE(m->rowCount(root), 0);
    QCOMPARE(m->data(root, SchemaTreeModel::LoadStateRole).toInt(), int(SchemaTreeModel::Idle));
    QVERIFY(!m->isConnected(id));

    // Disconnect while a deeper request is in flight.
    expand(m, root);
    const QModelIndex main = m->index(0, 0, root);
    expand(m, main);
    const QModelIndex tables = child(m, main, QStringLiteral("Tables"));
    m->fetchMore(tables);
    m->disconnectConnection(id);
    QTest::qWait(300);
    QCOMPARE(m->rowCount(root), 0);
    QVERIFY(!m->isConnected(id));
    // A fresh connection is not confused by the old session's answers.
    expand(m, root);
    QCOMPARE(m->rowCount(root), 1);
}

void TstSchemaTreeModel::removedConnectionWithRequestInFlight()
{
    ConnectionController c;
    SchemaTreeModel *m = c.tree();
    startController(c);
    const QString id = addConnection(c, QStringLiteral("Gone"), newFile(5));
    addConnection(c, QStringLiteral("Stays"), newFile(1));
    QTRY_COMPARE(m->rowCount(), 2);
    m->fetchMore(m->index(0, 0));
    c.remove(id);
    QTRY_COMPARE(m->rowCount(), 1);
    QTest::qWait(300); // the answer for the removed node arrives and is dropped
    QCOMPARE(text(m, m->index(0, 0), SchemaTreeModel::NameRole), QStringLiteral("Stays"));
    QCOMPARE(m->rowCount(m->index(0, 0)), 0);
}

void TstSchemaTreeModel::tenThousandTablesDoNotBlockTheEventLoop()
{
    ConnectionController c;
    SchemaTreeModel *m = c.tree();
    startController(c);
    addConnection(c, QStringLiteral("Big"), newFile(10'000));
    QTRY_COMPARE(m->rowCount(), 1);
    const QModelIndex root = m->index(0, 0);
    expand(m, root);
    const QModelIndex main = m->index(0, 0, root);
    expand(m, main);
    const QModelIndex tables = child(m, main, QStringLiteral("Tables"));

    qint64 maxGap = 0;
    QElapsedTimer sinceTick;
    QTimer ticker;
    ticker.setInterval(5);
    connect(&ticker, &QTimer::timeout, this,
            [&] { maxGap = std::max(maxGap, sinceTick.restart()); });
    sinceTick.start();
    ticker.start();
    QElapsedTimer clock;
    clock.start();
    m->fetchMore(tables);
    QTRY_COMPARE_WITH_TIMEOUT(m->data(tables, SchemaTreeModel::LoadStateRole).toInt(),
                              int(SchemaTreeModel::Loaded), 30000);
    const qint64 elapsed = clock.elapsed();
    ticker.stop();
    qInfo() << "expanding 10000 tables:" << elapsed << "ms, max event-loop gap" << maxGap << "ms";
    QCOMPARE(m->rowCount(tables), 10'000);
    QCOMPARE(m->data(m->index(9'999, 0, tables), SchemaTreeModel::SetSizeRole).toInt(), 10'000);
    QVERIFY2(maxGap <= 50, qPrintable(QString::number(maxGap)));
}

QTEST_MAIN(TstSchemaTreeModel)
#include "tst_schema_tree_model.moc"
