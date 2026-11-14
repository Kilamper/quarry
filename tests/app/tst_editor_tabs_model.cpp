#include "ui/connection_controller.h"
#include "ui/editor_tabs_model.h"

#include <quarry/editor_tab_store.h>

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace quarry;
using namespace quarry::ui;

namespace {

constexpr int kLoadMs = 10000;

} // namespace

// The editor tabs (H1.5) over the real EditorTabStore in a temporary quarry.db and the real SQLite
// plugin: the Mod+T rules, titles, persistence with the debounce, and the statement logic.
class TstEditorTabsModel : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    int m_files = 0;
    QString m_db;

    // A fresh quarry.db with a started ConnectionController on it.
    std::unique_ptr<ConnectionController> startController()
    {
        m_db = m_dir.filePath(QStringLiteral("quarry-%1.db").arg(++m_files));
        auto c = std::make_unique<ConnectionController>();
        c->setDatabasePath(m_db);
        c->setProviderDirectory(QStringLiteral(QUARRY_PLUGIN_DIR));
        c->start();
        [&] { QTRY_VERIFY_WITH_TIMEOUT(!c->model()->loading(), kLoadMs); }();
        return c;
    }

    QString addConnection(ConnectionController &c, const QString &name)
    {
        const QString file = m_dir.filePath(QStringLiteral("data-%1.db").arg(++m_files));
        c.prepare(QString());
        c.form()->setValue(0, file);
        QSignalSpy saved(&c, &ConnectionController::connectionSaved);
        const bool ok = c.save({ { QStringLiteral("id"), QString() },
                                 { QStringLiteral("providerId"), QStringLiteral("sqlite") },
                                 { QStringLiteral("name"), name },
                                 { QStringLiteral("colorIndex"), 2 },
                                 { QStringLiteral("production"), false },
                                 { QStringLiteral("passwordMode"), 1 } });
        if (!ok || !saved.wait(kLoadMs))
            return {};
        return saved.at(0).at(0).toString();
    }

    // A model on the current quarry.db, started and loaded.
    std::unique_ptr<EditorTabsModel> startModel(ConnectionController &c, int saveDelay = 30)
    {
        auto m = std::make_unique<EditorTabsModel>();
        m->setDatabasePath(m_db);
        m->setConnections(&c);
        m->setSaveDelay(saveDelay);
        m->setSplitDelay(20);
        m->start();
        [&] { QTRY_VERIFY_WITH_TIMEOUT(m->loaded(), kLoadMs); }();
        return m;
    }

    // What quarry.db holds, read by a second store.
    QList<EditorTab> stored()
    {
        EditorTabStore store(m_db);
        QSignalSpy loaded(&store, &EditorTabStore::loaded);
        store.load();
        if (!loaded.wait(kLoadMs))
            return {};
        return qvariant_cast<QList<EditorTab>>(loaded.at(0).at(0));
    }

    static QString idOf(const QUuid &id) { return id.toString(QUuid::WithoutBraces); }

    struct Env
    {
        std::unique_ptr<ConnectionController> controller;
        std::unique_ptr<EditorTabsModel> model;
    };

    Env sqliteTab(const QString &text)
    {
        Env e;
        e.controller = startController();
        const QString conn = addConnection(*e.controller, QStringLiteral("sqlite"));
        e.model = startModel(*e.controller, 60000);
        e.model->openTab(conn);
        e.model->setText(0, text);
        return e;
    }

    static int indexAt(EditorTabsModel &m, qsizetype pos)
    {
        return m.statementAt(0, static_cast<int>(pos)).value(QStringLiteral("index")).toInt();
    }

private slots:
    void modelContract()
    {
        auto c = startController();
        const QString conn = addConnection(*c, QStringLiteral("one"));
        QVERIFY(!conn.isEmpty());
        auto m = startModel(*c);
        QAbstractItemModelTester tester(m.get(),
                                        QAbstractItemModelTester::FailureReportingMode::Fatal);
        m->openTab(conn);
        m->openTab({});
        m->setText(1, QStringLiteral("select 1"));
        m->setConnection(1, QString());
        m->closeTab(0);
        m->openTab({});
        m->closeTab(1);
        m->closeTab(0);
        QCOMPARE(m->count(), 0);
        QCOMPARE(m->currentIndex(), -1);
    }

    // Mod+T: the connection of the current tree node, else that of the active tab, else none.
    void newTabConnectionRules()
    {
        auto c = startController();
        const QString a = addConnection(*c, QStringLiteral("a"));
        const QString b = addConnection(*c, QStringLiteral("b"));
        QVERIFY(!a.isEmpty() && !b.isEmpty());
        auto m = startModel(*c);

        m->openTab({}); // no tree node, no tab: no connection
        QCOMPARE(m->connectionIdOf(0), QString());
        m->openTab(a); // tree node
        QCOMPARE(m->connectionIdOf(1), a);
        m->openTab({}); // no tree node: the active tab's
        QCOMPARE(m->connectionIdOf(2), a);
        m->openTab(b); // the tree node wins over the active tab
        QCOMPARE(m->connectionIdOf(3), b);
        m->openTab(QStringLiteral("{00000000-0000-0000-0000-000000000042}")); // unknown: none
        QCOMPARE(m->connectionIdOf(4), QString());
        QCOMPARE(m->currentIndex(), 4);
        m->setCurrentIndex(0);
        m->openTab({});
        QCOMPARE(m->connectionIdOf(5), QString()); // active tab had none
    }

    void titlesAreUniqueAmongOpenTabs()
    {
        auto c = startController();
        auto m = startModel(*c);
        for (int i = 0; i < 3; ++i)
            m->openTab({});
        QCOMPARE(m->titleOf(0), QStringLiteral("Query 1"));
        QCOMPARE(m->titleOf(2), QStringLiteral("Query 3"));
        m->closeTab(1);
        m->openTab({});
        QCOMPARE(m->titleOf(2), QStringLiteral("Query 2")); // the free number is reused
        QCOMPARE(m->currentIndex(), 2);
    }

    void closingMovesTheCurrentTab()
    {
        auto c = startController();
        auto m = startModel(*c);
        for (int i = 0; i < 3; ++i)
            m->openTab({});
        m->setCurrentIndex(1);
        m->closeTab(0); // before the current one: it keeps pointing at the same tab
        QCOMPARE(m->currentIndex(), 0);
        QCOMPARE(m->titleOf(0), QStringLiteral("Query 2"));
        m->closeTab(0); // the current one: the next takes its place
        QCOMPARE(m->titleOf(m->currentIndex()), QStringLiteral("Query 3"));
        m->cycle(1);
        QCOMPARE(m->count(), 1);
        QCOMPARE(m->currentIndex(), 0);
    }

    void cycleWraps()
    {
        auto c = startController();
        auto m = startModel(*c);
        for (int i = 0; i < 3; ++i)
            m->openTab({});
        m->cycle(1);
        QCOMPARE(m->currentIndex(), 0);
        m->cycle(-1);
        QCOMPARE(m->currentIndex(), 2);
    }

    void restoresTabsTextCursorOrderAndConnection()
    {
        auto c = startController();
        const QString conn = addConnection(*c, QStringLiteral("saved"));
        {
            auto m = startModel(*c);
            m->openTab(conn);
            m->openTab({});
            m->openTab({});
            m->setText(0, QStringLiteral("select 1;\nselect 2;"));
            m->setCursor(0, 12);
            m->setText(2, QStringLiteral("third"));
            m->closeTab(1); // positions are renumbered
        }
        auto m = startModel(*c);
        QCOMPARE(m->count(), 2);
        QCOMPARE(m->currentIndex(), 0);
        QCOMPARE(m->titleOf(0), QStringLiteral("Query 1"));
        QCOMPARE(m->textOf(0), QStringLiteral("select 1;\nselect 2;"));
        QCOMPARE(m->cursorOf(0), 12);
        QCOMPARE(m->connectionIdOf(0), conn);
        QCOMPARE(m->titleOf(1), QStringLiteral("Query 3"));
        QCOMPARE(m->textOf(1), QStringLiteral("third"));
        QCOMPARE(m->connectionIdOf(1), conn); // opened from the tab that had it
    }

    void saveIsDebouncedAndFlushedOnDemand()
    {
        auto c = startController();
        auto m = startModel(*c, 1000);
        m->openTab({}); // stored at once
        QTRY_COMPARE_WITH_TIMEOUT(stored().size(), 1, kLoadMs);
        m->setText(0, QStringLiteral("a"));
        m->setText(0, QStringLiteral("ab"));
        QCOMPARE(stored().value(0).text, QString()); // not yet: the debounce is running
        m->setText(0, QStringLiteral("abc"));
        QTRY_COMPARE_WITH_TIMEOUT(stored().value(0).text, QStringLiteral("abc"), kLoadMs);
        m->setSaveDelay(60000);
        m->setText(0, QStringLiteral("abcd"));
        m->flush();
        QTRY_COMPARE_WITH_TIMEOUT(stored().value(0).text, QStringLiteral("abcd"), kLoadMs);
    }

    void destroyingTheModelFlushes()
    {
        auto c = startController();
        {
            auto m = startModel(*c, 60000);
            m->openTab({});
            m->setText(0, QStringLiteral("typed just before quitting"));
            m->setCursor(0, 5);
        }
        const auto tabs = stored();
        QCOMPARE(tabs.size(), 1);
        QCOMPARE(tabs.at(0).text, QStringLiteral("typed just before quitting"));
        QCOMPARE(tabs.at(0).cursorPosition, 5);
    }

    void closingDeletesTheStoredText()
    {
        auto c = startController();
        auto m = startModel(*c);
        m->openTab({});
        m->openTab({});
        m->setText(1, QStringLiteral("secret"));
        m->flush();
        QTRY_COMPARE_WITH_TIMEOUT(stored().size(), 2, kLoadMs);
        QVERIFY(m->hasText(1));
        QVERIFY(!m->hasText(0));
        m->closeTab(1);
        QTRY_COMPARE_WITH_TIMEOUT(stored().size(), 1, kLoadMs);
        QCOMPARE(stored().value(0).title, QStringLiteral("Query 1"));
    }

    void connectionChangeIsStoredAtOnce()
    {
        auto c = startController();
        const QString conn = addConnection(*c, QStringLiteral("x"));
        auto m = startModel(*c, 60000);
        m->openTab({});
        QSignalSpy changed(m.get(), &EditorTabsModel::dataChanged);
        m->setConnection(0, conn);
        QVERIFY(!changed.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(stored().value(0).connectionId.has_value(), kLoadMs);
        QCOMPARE(idOf(*stored().value(0).connectionId), conn);
        QCOMPARE(m->index(0).data(EditorTabsModel::ConnectionNameRole).toString(),
                 QStringLiteral("x"));
        QCOMPARE(m->index(0).data(EditorTabsModel::ColorIndexRole).toInt(), 2);
        QCOMPARE(m->index(0).data(EditorTabsModel::DefinitionRole).toString(),
                 QStringLiteral("SQL"));
    }

    // Deleting a connection leaves its tabs without one (the store does it, the model follows).
    void deletingAConnectionDetachesItsTabs()
    {
        auto c = startController();
        const QString conn = addConnection(*c, QStringLiteral("gone"));
        auto m = startModel(*c, 60000);
        m->openTab(conn);
        m->setText(0, QStringLiteral("select 1"));
        QSignalSpy removed(c.get(), &ConnectionController::connectionRemoved);
        c->remove(conn);
        QVERIFY(removed.wait(kLoadMs));
        QCOMPARE(m->connectionIdOf(0), QString());
        m->flush(); // the pending text is saved without the dead connection id
        QTRY_VERIFY_WITH_TIMEOUT(
            stored().size() == 1 && stored().value(0).text == QLatin1String("select 1"), kLoadMs);
        QVERIFY(!stored().value(0).connectionId.has_value());
    }

    void aTabOpenedBeforeTheLoadSurvivesIt()
    {
        auto c = startController();
        {
            auto m = startModel(*c);
            m->openTab({});
            m->setText(0, QStringLiteral("old"));
        }
        auto m = std::make_unique<EditorTabsModel>();
        m->setDatabasePath(m_db);
        m->setConnections(c.get());
        m->setSaveDelay(30);
        m->openTab({}); // before start()
        m->start();
        QTRY_VERIFY_WITH_TIMEOUT(m->loaded(), kLoadMs);
        QCOMPARE(m->count(), 2);
        QCOMPARE(m->textOf(0), QStringLiteral("old"));
        QCOMPARE(m->currentIndex(), 1); // the one the user just opened stays current
        QCOMPARE(m->titleOf(1),
                 QStringLiteral("Query 1")); // (a clash with the stored one is tolerated)
    }

    // ---- statements -----------------------------------------------------------------------

private slots:
    void definitionComesFromTheDialect()
    {
        auto e = sqliteTab({});
        QCOMPARE(e.model->definitionOf(0), QStringLiteral("SQL"));
        e.model->setConnection(0, QString());
        QCOMPARE(e.model->definitionOf(0), QStringLiteral("SQL")); // no connection: the fallback
    }

    void statementUnderTheCursor()
    {
        const QString text = QStringLiteral("SELECT 1;\n"
                                            "SELECT 'a;b' -- not ; here\n"
                                            "  FROM t;\n"
                                            "/* c; */ SELECT 3");
        auto e = sqliteTab(text);
        auto &m = *e.model;
        QCOMPARE(m.statementAt(0, 0).value(QStringLiteral("count")).toInt(), 3);
        QCOMPARE(indexAt(m, 0), 0);
        QCOMPARE(indexAt(m, 5), 0);
        QCOMPARE(indexAt(m, text.indexOf(QLatin1String("1;")) + 2), 0); // right after the ';'
        QCOMPARE(indexAt(m, text.indexOf(QLatin1String("a;b")) + 1), 1); // inside a string
        QCOMPARE(indexAt(m, text.indexOf(QLatin1String("not"))), 1); // inside a comment
        QCOMPARE(indexAt(m, text.indexOf(QLatin1String("FROM"))), 1);
        QCOMPARE(indexAt(m, text.indexOf(QLatin1String("c; */"))), 2); // comment before a statement
        QCOMPARE(indexAt(m, text.size()), 2); // the very end

        const QVariantMap second = m.statementAt(0, text.indexOf(QLatin1String("FROM")));
        QCOMPARE(second.value(QStringLiteral("startLine")).toInt(), 2);
        QCOMPARE(second.value(QStringLiteral("endLine")).toInt(), 3);
        QCOMPARE(m.statementAt(0, 0).value(QStringLiteral("endLine")).toInt(), 1);
    }

    void betweenStatementsTheNearestPreviousOneCounts()
    {
        auto e = sqliteTab(QStringLiteral("SELECT 1;\n\n\nSELECT 2;\n\n"));
        QCOMPARE(indexAt(*e.model, 11), 0); // blank lines after the first
        QCOMPARE(indexAt(*e.model, 22), 1); // after the last
        e.model->setText(0, QStringLiteral("\n\nSELECT 1;"));
        QCOMPARE(indexAt(*e.model, 0), 0); // before the first: the first
    }

    void emptyAndCommentOnlyTextHasNoStatement()
    {
        auto e = sqliteTab(QString());
        QCOMPARE(e.model->statementAt(0, 0).value(QStringLiteral("index")).toInt(), -1);
        QCOMPARE(e.model->statementAt(0, 0).value(QStringLiteral("count")).toInt(), 0);
        e.model->setText(0, QStringLiteral("-- nothing\n/* at all */\n"));
        QVERIFY(e.model->run(0, false, 3).value(QStringLiteral("message")).toString()
                == QLatin1String("Nothing to run."));
        QVERIFY(e.model->run(0, true, 3).value(QStringLiteral("message")).toString()
                == QLatin1String("Nothing to run."));
    }

    void noConnectionHasNoStatements()
    {
        auto c = startController();
        auto m = startModel(*c);
        m->openTab({});
        m->setText(0, QStringLiteral("select 1"));
        QSignalSpy run(m.get(), &EditorTabsModel::runRequested);
        const QVariantMap plan = m->run(0, false, 0);
        QCOMPARE(plan.value(QStringLiteral("ok")).toBool(), false);
        QCOMPARE(plan.value(QStringLiteral("message")).toString(),
                 QStringLiteral("Choose a connection first."));
        QCOMPARE(m->statementAt(0, 0).value(QStringLiteral("index")).toInt(), -1);
        QCOMPARE(run.size(), 0);
        QCOMPARE(m->run(7, false, 0).value(QStringLiteral("message")).toString(),
                 QStringLiteral("No query is open."));
    }

    void runPlansTheRangesAndAnnounces()
    {
        const QString text
            = QStringLiteral("SELECT 1;\nSELECT 2\n  FROM t;\nSELECT 3;\nSELECT 4;\nSELECT 5");
        auto e = sqliteTab(text);
        auto &m = *e.model;
        QSignalSpy run(&m, &EditorTabsModel::runRequested);

        const QVariantMap one = m.run(0, false, text.indexOf(QLatin1String("FROM")));
        QVERIFY(one.value(QStringLiteral("ok")).toBool());
        QCOMPARE(one.value(QStringLiteral("kind")).toString(), QStringLiteral("statement"));
        QCOMPARE(one.value(QStringLiteral("message")).toString(),
                 QStringLiteral("Statement 2 of 5, lines 2–3"));
        const QVariantList ranges = one.value(QStringLiteral("ranges")).toList();
        QCOMPARE(ranges.size(), 1);
        const QVariantMap r = ranges.at(0).toMap();
        QCOMPARE(text.mid(r.value(QStringLiteral("begin")).toInt(),
                          r.value(QStringLiteral("end")).toInt()
                              - r.value(QStringLiteral("begin")).toInt()),
                 QStringLiteral("SELECT 2\n  FROM t"));
        QCOMPARE(run.size(), 1);
        QCOMPARE(run.at(0).at(0).toMap().value(QStringLiteral("tabId")).toString(), m.idOf(0));

        const QVariantMap first = m.run(0, false, 0);
        QCOMPARE(first.value(QStringLiteral("message")).toString(),
                 QStringLiteral("Statement 1 of 5, line 1"));

        const QVariantMap all = m.run(0, true, 0);
        QCOMPARE(all.value(QStringLiteral("kind")).toString(), QStringLiteral("all"));
        QCOMPARE(all.value(QStringLiteral("message")).toString(), QStringLiteral("5 statements"));
        QCOMPARE(all.value(QStringLiteral("ranges")).toList().size(), 5);
        QCOMPARE(run.size(), 3);

        m.setText(0, QStringLiteral("SELECT 1"));
        QCOMPARE(m.run(0, true, 0).value(QStringLiteral("message")).toString(),
                 QStringLiteral("1 statement"));
    }

    // The split follows the text a little after the last edit; a run never uses a stale one.
    void splitIsDebouncedButRunIsFresh()
    {
        auto e = sqliteTab(QStringLiteral("SELECT 1;"));
        auto &m = *e.model;
        QCOMPARE(m.statementAt(0, 0).value(QStringLiteral("count")).toInt(), 1);
        QSignalSpy changed(&m, &EditorTabsModel::statementsChanged);
        m.setText(0, QStringLiteral("SELECT 1; SELECT 2;"));
        QCOMPARE(m.statementAt(0, 0).value(QStringLiteral("count")).toInt(), 1); // last split
        QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(), kLoadMs);
        QCOMPARE(m.statementAt(0, 0).value(QStringLiteral("count")).toInt(), 2);
        m.setText(0, QStringLiteral("SELECT 1; SELECT 2; SELECT 3;"));
        QCOMPARE(m.run(0, true, 0).value(QStringLiteral("count")).toInt(), 3);
    }

    void unicodeTextKeepsLinesAndOffsets()
    {
        const QString text = QStringLiteral("SELECT 'é\U0001F600';\nSELECT 2");
        auto e = sqliteTab(text);
        const QVariantMap second = e.model->statementAt(0, text.size());
        QCOMPARE(second.value(QStringLiteral("startLine")).toInt(), 2);
        QCOMPARE(text.mid(second.value(QStringLiteral("begin")).toInt()),
                 QStringLiteral("SELECT 2"));
    }
};

QTEST_MAIN(TstEditorTabsModel)
#include "tst_editor_tabs_model.moc"
