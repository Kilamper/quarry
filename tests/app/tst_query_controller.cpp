#include "ui/connection_controller.h"
#include "ui/editor_tabs_model.h"
#include "ui/query_controller.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace quarry;
using namespace quarry::ui;

namespace {

constexpr int kWaitMs = 10000;
constexpr int kSlowMs = 60000; // a debug build counts slowly; the cancel ends it at once anyway

// Runs for far longer than any test waits and is interruptible (sqlite3_interrupt): a fast CI
// machine must not finish it before the cancel.
const QString kSlowSql = QStringLiteral(
    "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x < 100000000000) "
    "SELECT count(*) FROM c");

} // namespace

// QueryController (H1.6) over the real SQLite plugin and files in a temporary directory: the
// plans, the stop at the first error, the error position, cancelling, the per-tab sessions and
// the SqlSafety confirmation. The keychain is never used (no connection of these tests has a
// password).
class TstQueryController : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    int m_files = 0;

    // Declared in this order so the QueryController dies before the plugins' registry.
    struct Env
    {
        std::unique_ptr<ConnectionController> controller;
        std::unique_ptr<EditorTabsModel> tabs;
        std::unique_ptr<QueryController> queries;
        QString connection;
    };

    QString addConnection(ConnectionController &c, const QString &name, bool production = false)
    {
        const QString file = m_dir.filePath(QStringLiteral("data-%1.db").arg(++m_files));
        c.prepare(QString());
        c.form()->setValue(0, file);
        QSignalSpy saved(&c, &ConnectionController::connectionSaved);
        const bool ok = c.save({ { QStringLiteral("id"), QString() },
                                 { QStringLiteral("providerId"), QStringLiteral("sqlite") },
                                 { QStringLiteral("name"), name },
                                 { QStringLiteral("colorIndex"), 2 },
                                 { QStringLiteral("production"), production },
                                 { QStringLiteral("passwordMode"), 1 } });
        if (!ok || !saved.wait(kWaitMs))
            return {};
        return saved.at(0).at(0).toString();
    }

    // Saves a profile straight into the store, with the password mode the SQLite form would not
    // allow (the SQLite engine has no secret field). No password is ever stored.
    QString addProfile(ConnectionController &c, const QString &name, PasswordMode mode)
    {
        ConnectionProfile p;
        p.id = QUuid::createUuid();
        p.name = name;
        p.providerId = QStringLiteral("sqlite");
        p.values = { { QStringLiteral("file"),
                       m_dir.filePath(QStringLiteral("data-%1.db").arg(++m_files)) } };
        p.passwordMode = mode;
        c.model()->store()->save(p);
        const QString id = p.id.toString(QUuid::WithoutBraces);
        (void)QTest::qWaitFor([&] { return c.model()->indexOf(id) >= 0; }, kWaitMs);
        return id;
    }

    Env start(bool production = false)
    {
        Env e;
        e.controller = std::make_unique<ConnectionController>();
        e.controller->setDatabasePath(
            m_dir.filePath(QStringLiteral("quarry-%1.db").arg(++m_files)));
        e.controller->setProviderDirectory(QStringLiteral(QUARRY_PLUGIN_DIR));
        e.controller->start();
        [&] { QTRY_VERIFY_WITH_TIMEOUT(!e.controller->model()->loading(), kWaitMs); }();
        e.connection = addConnection(*e.controller, QStringLiteral("db"), production);
        e.tabs = std::make_unique<EditorTabsModel>();
        e.tabs->setDatabasePath(m_dir.filePath(QStringLiteral("tabs-%1.db").arg(++m_files)));
        e.tabs->setConnections(e.controller.get());
        e.tabs->setSaveDelay(60000);
        e.tabs->start();
        [&] { QTRY_VERIFY_WITH_TIMEOUT(e.tabs->loaded(), kWaitMs); }();
        e.queries = std::make_unique<QueryController>();
        e.queries->setTabs(e.tabs.get());
        return e;
    }

    // Opens a tab on the connection and returns its row.
    static int openTab(Env &e, const QString &connection = {})
    {
        return e.tabs->openTab(connection.isEmpty() ? e.connection : connection);
    }

    static QueryState *stateOf(Env &e, int row) { return e.queries->stateOf(e.tabs->idOf(row)); }

    // Sets the text and runs all of it (or the statement at the end).
    static QVariantMap run(Env &e, int row, const QString &sql, bool all = true)
    {
        e.tabs->setText(row, sql);
        return e.tabs->run(row, all, static_cast<int>(sql.size()));
    }

    static void waitFor(QueryState *s, const QString &state)
    {
        QTRY_COMPARE_WITH_TIMEOUT(s->state(), state, kSlowMs);
    }

private slots:
    void singleStatementAndRunAll()
    {
        Env e = start();
        const int row = openTab(e);
        QueryState *s = stateOf(e, row);
        QAbstractItemModelTester tester(s->messages(),
                                        QAbstractItemModelTester::FailureReportingMode::Fatal);
        QVERIFY(s != nullptr);
        QCOMPARE(s->state(), QStringLiteral("idle"));
        QCOMPARE(e.queries->sessionCount(), 0); // nothing before the first run
        QVERIFY(!e.queries->hasSession(e.tabs->idOf(row)));

        QSignalSpy said(e.queries.get(), &QueryController::announcement);
        const QVariantMap plan = run(
            e, row,
            QStringLiteral("CREATE TABLE t(a, b); INSERT INTO t VALUES (1,'x'),(2,'y'),(3,'z');"
                           " SELECT a, b FROM t"));
        QVERIFY(plan.value(QStringLiteral("ok")).toBool());
        QCOMPARE(s->state(), QStringLiteral("running")); // at once, never blocking
        QVERIFY(s->running());
        waitFor(s, QStringLiteral("done"));
        QVERIFY(!s->running());
        QVERIFY(e.queries->hasSession(e.tabs->idOf(row)));

        QCOMPARE(s->rows(), 3);
        QCOMPARE(s->columns(), (QStringList { QStringLiteral("a"), QStringLiteral("b") }));
        QVERIFY(s->atEnd());
        QVERIFY(s->hasSummary());
        QVERIFY(s->statusText().startsWith(QStringLiteral("3 rows · ")));
        QCOMPARE(s->messages()->count(), 3);
        const auto text = [&](int i) {
            return s->messages()->data(s->messages()->index(i), QueryMessagesModel::TextRole);
        };
        QCOMPARE(text(0).toString(), QStringLiteral("Statement 1 of 3: Done"));
        QCOMPARE(text(1).toString(), QStringLiteral("Statement 2 of 3: 3 rows affected"));
        QCOMPARE(text(2).toString(), QStringLiteral("Statement 3 of 3: 3 rows"));

        // Start (polite) per statement, then the end.
        QStringList said2;
        for (const auto &args : said)
            said2 << args.at(0).toString();
        QCOMPARE(said2.value(0), QStringLiteral("Running statement 1 of 3"));
        QCOMPARE(said2.value(1), QStringLiteral("Running statement 2 of 3"));
        QCOMPARE(said2.value(2), QStringLiteral("Running statement 3 of 3"));
        QVERIFY(said2.value(3).startsWith(QStringLiteral("Done: 3 rows in ")));
        QVERIFY(!said.at(3).at(1).toBool());

        // One statement (Mod+Enter): numbered like the editor says it ("Statement 3 of 3").
        e.tabs->setText(row,
                        QStringLiteral("SELECT 1; DELETE FROM t WHERE a = 1; SELECT * FROM t"));
        said.clear();
        e.tabs->run(row, false, 25); // inside the DELETE
        waitFor(s, QStringLiteral("done"));
        QCOMPARE(s->messages()->count(), 1);
        QCOMPARE(
            s->messages()->data(s->messages()->index(0), QueryMessagesModel::TextRole).toString(),
            QStringLiteral("Statement 2 of 3: 1 row affected"));
        QCOMPARE(said.value(0).value(0).toString(), QStringLiteral("Running statement 2 of 3"));
        QCOMPARE(said.value(1).value(0).toString(), QStringLiteral("1 row affected"));
    }

    void stopsAtTheFirstError()
    {
        Env e = start();
        const int row = openTab(e);
        QueryState *s = stateOf(e, row);
        QSignalSpy failed(e.queries.get(), &QueryController::planFailed);
        QSignalSpy said(e.queries.get(), &QueryController::announcement);
        run(e, row,
            QStringLiteral("CREATE TABLE t(a); SELECT * FROM nope; INSERT INTO t VALUES (1)"));
        waitFor(s, QStringLiteral("failed"));
        QCOMPARE(failed.count(), 1);
        QCOMPARE(s->messages()->count(), 2); // the first statement's result, then the error
        const QModelIndex err = s->messages()->index(1);
        QCOMPARE(s->messages()->data(err, QueryMessagesModel::KindRole).toString(),
                 QStringLiteral("error"));
        QVERIFY(!s->messages()->data(err, QueryMessagesModel::TextRole).toString().isEmpty());
        QVERIFY(!s->messages()->data(err, QueryMessagesModel::TypeRole).toString().isEmpty());
        QVERIFY(!s->messages()->data(err, QueryMessagesModel::DetailRole).toString().isEmpty());
        QCOMPARE(s->statusText(), QStringLiteral("Query failed"));
        // The error is assertive; the third statement never ran.
        QVERIFY(said.last().at(1).toBool());
        run(e, row, QStringLiteral("SELECT * FROM t"));
        waitFor(s, QStringLiteral("done"));
        QCOMPARE(s->rows(), 0);
        QVERIFY(s->errorOffset() < 0); // the next run clears the error
    }

    void errorPositionIsMappedToTheTabText()
    {
        Env e = start();
        const int row = openTab(e);
        QueryState *s = stateOf(e, row);
        QSignalSpy said(e.queries.get(), &QueryController::announcement);
        const QString sql = QStringLiteral("SELECT 1;\nSELECT * frm t;\nSELECT 3");
        run(e, row, sql);
        waitFor(s, QStringLiteral("failed"));
        const int begin = static_cast<int>(sql.indexOf(QLatin1String("SELECT * frm")));
        QVERIFY2(s->errorOffset() >= begin, qPrintable(QString::number(s->errorOffset())));
        QVERIFY(s->errorOffset() < sql.indexOf(QLatin1Char('3')));
        QCOMPARE(s->errorLine(), 2);
        QVERIFY(s->errorColumn() >= 1);
        QVERIFY(s->errorLength() >= 1);
        QVERIFY(said.last().at(0).toString().contains(
            QStringLiteral("Error at line 2, column %1").arg(s->errorColumn())));
        const QModelIndex err = s->messages()->index(s->messages()->count() - 1);
        QCOMPARE(s->messages()->data(err, QueryMessagesModel::LineRole).toInt(), 2);

        // Editing the text makes the position meaningless: the view clears it.
        s->clearError();
        QCOMPARE(s->errorOffset(), -1);

        // A text edited while the statement ran gets no position at all.
        e.tabs->setText(row, QStringLiteral("SELECT * frm t"));
        e.tabs->run(row, true, 0);
        e.tabs->setText(row, QStringLiteral("SELECT * frm t "));
        waitFor(s, QStringLiteral("failed"));
        QCOMPARE(s->errorOffset(), -1);
    }

    void cancelEndsAsCancelledNotAsError()
    {
        Env e = start();
        const int row = openTab(e);
        const QString tab = e.tabs->idOf(row);
        QueryState *s = stateOf(e, row);
        QSignalSpy failed(e.queries.get(), &QueryController::planFailed);
        QSignalSpy said(e.queries.get(), &QueryController::announcement);
        QElapsedTimer clock;
        run(e, row, kSlowSql + QStringLiteral("; SELECT 2"));
        clock.start();
        QTest::qWait(300);
        QCOMPARE(s->state(), QStringLiteral("running"));
        QVERIFY(s->statusText().startsWith(QStringLiteral("Running… 00:0")));
        e.queries->cancel(tab);
        waitFor(s, QStringLiteral("cancelled"));
        QVERIFY2(clock.elapsed() < 20000, "cancel must interrupt the statement");
        QCOMPARE(failed.count(), 0);
        QCOMPARE(s->statusText(), QStringLiteral("Query cancelled"));
        QCOMPARE(
            s->messages()->data(s->messages()->index(0), QueryMessagesModel::KindRole).toString(),
            QStringLiteral("cancelled"));
        QVERIFY(s->errorOffset() < 0);
        QCOMPARE(said.last().at(0).toString(), QStringLiteral("Query cancelled"));
        QVERIFY(!said.last().at(1).toBool()); // polite
        // The second statement did not run, and the session still works.
        run(e, row, QStringLiteral("SELECT 7"));
        waitFor(s, QStringLiteral("done"));
        QCOMPARE(s->rows(), 1);
        // Cancelling an idle tab does nothing.
        e.queries->cancel(tab);
        QCOMPARE(s->state(), QStringLiteral("done"));
    }

    void theElapsedTimeTicks()
    {
        Env e = start();
        const int row = openTab(e);
        QueryState *s = stateOf(e, row);
        QSignalSpy changed(s, &QueryState::changed);
        run(e, row, kSlowSql);
        QTRY_VERIFY_WITH_TIMEOUT(s->elapsedText() == QLatin1String("00:01"), 5000);
        QCOMPARE(s->statusText(), QStringLiteral("Running… 00:01"));
        QVERIFY(changed.count() >= 2);
        e.queries->cancel(e.tabs->idOf(row));
        waitFor(s, QStringLiteral("cancelled"));
    }

    void aNewPlanCancelsTheOldOne()
    {
        Env e = start();
        const int row = openTab(e);
        QueryState *s = stateOf(e, row);
        QSignalSpy failed(e.queries.get(), &QueryController::planFailed);
        run(e, row, kSlowSql);
        QTest::qWait(200);
        QCOMPARE(s->state(), QStringLiteral("running"));
        run(e, row, QStringLiteral("SELECT 41 + 1 AS answer"));
        waitFor(s, QStringLiteral("done"));
        // The cancel of the old statement must not stop (or show up in) the new plan.
        QTest::qWait(200);
        QCOMPARE(s->state(), QStringLiteral("done"));
        QCOMPARE(s->rows(), 1);
        QCOMPARE(s->columns(), QStringList { QStringLiteral("answer") });
        QCOMPARE(s->messages()->count(), 1);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(e.queries->sessionCount(), 1);
    }

    void tabsRunIndependently()
    {
        Env e = start();
        const int a = openTab(e);
        const int b = openTab(e);
        QVERIFY(a != b);
        QueryState *sa = stateOf(e, a);
        QueryState *sb = stateOf(e, b);
        run(e, a, kSlowSql);
        run(e, b, QStringLiteral("SELECT 5"));
        waitFor(sb, QStringLiteral("done"));
        QCOMPARE(sa->state(), QStringLiteral("running")); // the other tab was not touched
        QCOMPARE(e.queries->sessionCount(), 2);
        // `current` follows the current tab.
        e.tabs->setCurrentIndex(a);
        QCOMPARE(e.queries->current(), sa);
        e.tabs->setCurrentIndex(b);
        QCOMPARE(e.queries->current(), sb);
        e.queries->cancel(e.tabs->idOf(a));
        waitFor(sa, QStringLiteral("cancelled"));
    }

    void theSessionClosesWithTheTabTheConnectionChangeAndTheConnectionDelete()
    {
        Env e = start();
        const QString other = addConnection(*e.controller, QStringLiteral("other"));
        QVERIFY(!other.isEmpty());
        QSignalSpy count(e.queries.get(), &QueryController::sessionsChanged);

        // The tab closes.
        int row = openTab(e);
        QString tab = e.tabs->idOf(row);
        run(e, row, QStringLiteral("SELECT 1"));
        waitFor(stateOf(e, row), QStringLiteral("done"));
        QCOMPARE(e.queries->sessionCount(), 1);
        e.tabs->closeTab(row);
        QCOMPARE(e.queries->sessionCount(), 0);
        QVERIFY(!e.queries->hasSession(tab));
        QVERIFY(e.queries->stateOf(tab) == nullptr);

        // Its connection changes: the session and the results go.
        row = openTab(e);
        tab = e.tabs->idOf(row);
        run(e, row, QStringLiteral("SELECT 1"));
        waitFor(stateOf(e, row), QStringLiteral("done"));
        QVERIFY(e.queries->hasSession(tab));
        e.tabs->setConnection(row, other);
        QVERIFY(!e.queries->hasSession(tab));
        QCOMPARE(stateOf(e, row)->state(), QStringLiteral("idle"));
        run(e, row, QStringLiteral("SELECT 2")); // a new session, on the new connection
        waitFor(stateOf(e, row), QStringLiteral("done"));
        QVERIFY(e.queries->hasSession(tab));

        // The connection is deleted.
        QSignalSpy removed(e.controller.get(), &ConnectionController::connectionRemoved);
        e.controller->remove(other);
        QTRY_COMPARE_WITH_TIMEOUT(removed.count(), 1, kWaitMs);
        QVERIFY(!e.queries->hasSession(tab));
        QCOMPARE(e.queries->sessionCount(), 0);
        QVERIFY(count.count() > 0);

        // A tab that is running when it closes: nothing is left behind and nothing crashes.
        row = openTab(e);
        run(e, row, kSlowSql);
        QTest::qWait(100);
        e.tabs->closeTab(row);
        QCOMPARE(e.queries->sessionCount(), 0);
    }

    void riskyStatementsAreListedAndNothingRunsUntilConfirmed()
    {
        Env e = start();
        const int row = openTab(e);
        const QString tab = e.tabs->idOf(row);
        QueryState *s = stateOf(e, row);
        run(e, row, QStringLiteral("CREATE TABLE t(a); INSERT INTO t VALUES (1),(2)"));
        waitFor(s, QStringLiteral("done"));

        QSignalSpy asked(e.queries.get(), &QueryController::confirmationRequested);
        QSignalSpy said(e.queries.get(), &QueryController::announcement);
        run(e, row,
            QStringLiteral(
                "SELECT 1;\nDELETE FROM t;\nUPDATE t SET a = 0;\nDELETE FROM t WHERE a = 1"));
        QCOMPARE(asked.count(), 1);
        const QVariantMap request = asked.at(0).at(0).toMap();
        QCOMPARE(request.value(QStringLiteral("tabId")).toString(), tab);
        QCOMPARE(request.value(QStringLiteral("connectionName")).toString(), QStringLiteral("db"));
        QVERIFY(!request.value(QStringLiteral("production")).toBool());
        const QVariantList items = request.value(QStringLiteral("items")).toList();
        QCOMPARE(items.size(), 2); // DELETE and UPDATE without WHERE; the others are fine
        QCOMPARE(items.at(0).toMap().value(QStringLiteral("risk")).toString(),
                 QStringLiteral("DELETE without WHERE"));
        QCOMPARE(items.at(0).toMap().value(QStringLiteral("label")).toString(),
                 QStringLiteral("Statement 2 of 4, line 2"));
        QCOMPARE(items.at(1).toMap().value(QStringLiteral("risk")).toString(),
                 QStringLiteral("UPDATE without WHERE"));
        // Nothing runs, not even the harmless first statement, and nothing is announced.
        QTest::qWait(300);
        QCOMPARE(s->state(), QStringLiteral("done")); // still the earlier run
        QCOMPARE(s->messages()->count(), 2);
        QVERIFY(said.isEmpty());

        // Decline: still nothing.
        e.queries->decline(tab);
        QTest::qWait(100);
        QCOMPARE(s->messages()->count(), 2);
        e.queries->confirm(tab); // there is nothing pending any more
        QTest::qWait(100);
        QCOMPARE(s->messages()->count(), 2);
        run(e, row, QStringLiteral("SELECT count(*) FROM t"));
        waitFor(s, QStringLiteral("done"));
        QCOMPARE(s->rows(), 1);

        // Confirm: the whole plan runs.
        asked.clear();
        run(e, row, QStringLiteral("SELECT 1;\nDELETE FROM t;"));
        QCOMPARE(asked.count(), 1);
        e.queries->confirm(tab);
        waitFor(s, QStringLiteral("done"));
        QCOMPARE(s->messages()->count(), 2);
        QCOMPARE(s->affectedRows(), 2); // both rows were still there
    }

    void dropAndTruncateNeedAProductionConnection()
    {
        Env prod = start(true);
        int row = openTab(prod);
        QSignalSpy asked(prod.queries.get(), &QueryController::confirmationRequested);
        run(prod, row, QStringLiteral("DROP TABLE IF EXISTS t"));
        QCOMPARE(asked.count(), 1);
        const QVariantMap request = asked.at(0).at(0).toMap();
        QVERIFY(request.value(QStringLiteral("production")).toBool());
        QCOMPARE(request.value(QStringLiteral("items"))
                     .toList()
                     .at(0)
                     .toMap()
                     .value(QStringLiteral("risk"))
                     .toString(),
                 QStringLiteral("DROP on a production connection"));
        prod.queries->decline(prod.tabs->idOf(row));

        Env dev = start(false);
        row = openTab(dev);
        QSignalSpy askedDev(dev.queries.get(), &QueryController::confirmationRequested);
        run(dev, row, QStringLiteral("DROP TABLE IF EXISTS t"));
        waitFor(stateOf(dev, row), QStringLiteral("done"));
        QCOMPARE(askedDev.count(), 0);
    }

    void aRunWithoutAConnectionOrWithAGoneOneNeverStartsASession()
    {
        Env e = start();
        const int row = e.tabs->openTab({});
        e.tabs->setConnection(row, QString());
        QSignalSpy requested(e.tabs.get(), &EditorTabsModel::runRequested);
        const QVariantMap plan = run(e, row, QStringLiteral("SELECT 1"));
        QVERIFY(!plan.value(QStringLiteral("ok")).toBool());
        QCOMPARE(requested.count(), 0);
        QCOMPARE(e.queries->sessionCount(), 0);
    }

    // "Ask every time": the first run asks, the answer is remembered for the other tabs, and a
    // dismissed prompt ends the plan as a cancellation.
    void askEveryTimePromptsOnceThenRemembers()
    {
        Env e = start();
        const QString ask
            = addProfile(*e.controller, QStringLiteral("Ask"), PasswordMode::AskEveryTime);
        const int a = openTab(e, ask);
        const int b = openTab(e, ask);
        QSignalSpy prompt(e.queries.get(), &QueryController::passwordRequested);
        run(e, a, QStringLiteral("SELECT 1"));
        QCOMPARE(prompt.count(), 1);
        QCOMPARE(prompt.at(0).at(0).toString(), ask);
        QCOMPARE(prompt.at(0).at(1).toString(), QStringLiteral("Ask"));
        QCOMPARE(prompt.at(0).at(2).toBool(), false);
        QCOMPARE(stateOf(e, a)->state(), QStringLiteral("running")); // waiting, nothing blocked
        QCOMPARE(e.queries->sessionCount(), 0);
        e.queries->providePassword(ask, QStringLiteral("hunter2"));
        waitFor(stateOf(e, a), QStringLiteral("done"));
        run(e, b, QStringLiteral("SELECT 2")); // remembered for this run of the app
        waitFor(stateOf(e, b), QStringLiteral("done"));
        QCOMPARE(prompt.count(), 1);

        // Dismissed: "Query cancelled", not an error.
        const QString other
            = addProfile(*e.controller, QStringLiteral("Other"), PasswordMode::AskEveryTime);
        const int c = openTab(e, other);
        run(e, c, QStringLiteral("SELECT 3"));
        QCOMPARE(prompt.count(), 2);
        e.queries->cancelPassword(other);
        QCOMPARE(stateOf(e, c)->state(), QStringLiteral("cancelled"));
        QCOMPARE(e.queries->sessionCount(), 2);
    }

    // "Saved": the password comes from the keychain backend, which here is a stub.
    void aSavedPasswordComesFromTheKeychain()
    {
        Env e = start();
        ConnectionController *c = e.controller.get();
        CredentialBackend backend;
        backend.read = [c](QUuid id) {
            QMetaObject::invokeMethod(
                c, [c, id] { c->onPasswordRead(id, QStringLiteral("secret")); },
                Qt::QueuedConnection);
        };
        backend.write = [](QUuid, QString) {};
        backend.remove = [](QUuid) {};
        backend.forget = [](QUuid) {};
        c->setCredentialBackend(backend);
        const QString saved = addProfile(*c, QStringLiteral("Saved"), PasswordMode::Saved);
        const int row = openTab(e, saved);
        run(e, row, QStringLiteral("SELECT 1"));
        QCOMPARE(stateOf(e, row)->state(), QStringLiteral("running"));
        waitFor(stateOf(e, row), QStringLiteral("done"));
        QVERIFY(e.queries->hasSession(e.tabs->idOf(row)));

        // A keychain failure is an error in the Messages, and cancelling while it waits works.
        backend.read = [c](QUuid id) {
            QMetaObject::invokeMethod(
                c,
                [c, id] {
                    c->onCredentialFailed(
                        id,
                        Error {
                            ErrorKind::Unsupported, QStringLiteral("No keychain."), {}, {}, {} });
                },
                Qt::QueuedConnection);
        };
        c->setCredentialBackend(backend);
        const int second = openTab(e, saved);
        run(e, second, QStringLiteral("SELECT 1"));
        waitFor(stateOf(e, second), QStringLiteral("failed"));
        QCOMPARE(stateOf(e, second)->messages()->count(), 1);
        QCOMPARE(e.queries->sessionCount(), 1);
    }
};

QTEST_MAIN(TstQueryController)
#include "tst_query_controller.moc"
