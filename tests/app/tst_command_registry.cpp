#include "ui/command_registry.h"

#include <QAbstractItemModelTester>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace quarry::ui;

class TstCommandRegistry : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    static Command *make(QObject *parent, const QString &id, const QString &title,
                         const QString &category = QStringLiteral("Test"))
    {
        auto *c = new Command(parent);
        c->setCommandId(id);
        c->setTitle(title);
        c->setCategory(category);
        return c;
    }

    static QStringList ids(const CommandRegistry &r)
    {
        QStringList out;
        for (int i = 0; i < r.rowCount(); ++i)
            out << r.data(r.index(i), CommandRegistry::IdRole).toString();
        return out;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("QuarryTest"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_command_registry"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
    }

    void init() { QSettings().clear(); }

    void registerFindUnregister()
    {
        CommandRegistry reg;
        QAbstractItemModelTester tester(&reg);
        QObject owner;
        Command *a = make(&owner, "a", "Alpha");
        QVERIFY(reg.registerCommand(a));
        QCOMPARE(reg.find("a"), a);
        QCOMPARE(reg.find("missing"), nullptr);
        QCOMPARE(reg.rowCount(), 1);
        QVERIFY(reg.unregisterCommand("a"));
        QVERIFY(!reg.unregisterCommand("a"));
        QCOMPARE(reg.find("a"), nullptr);
        QCOMPARE(reg.rowCount(), 0);
    }

    void rejectsDuplicateAndInvalid()
    {
        CommandRegistry reg;
        QObject owner;
        QVERIFY(reg.registerCommand(make(&owner, "a", "Alpha")));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("duplicate"));
        QVERIFY(!reg.registerCommand(make(&owner, "a", "Other")));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("without an id"));
        QVERIFY(!reg.registerCommand(make(&owner, "", "No id")));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("without an id"));
        QVERIFY(!reg.registerCommand(nullptr));
        QCOMPARE(reg.rowCount(), 1);
        QCOMPARE(reg.find("a")->title(), QStringLiteral("Alpha"));
    }

    void destroyedCommandLeavesRegistry()
    {
        CommandRegistry reg;
        QAbstractItemModelTester tester(&reg);
        auto *c = make(nullptr, "gone", "Gone");
        QVERIFY(reg.registerCommand(c));
        delete c;
        QCOMPARE(reg.rowCount(), 0);
        QCOMPARE(reg.find("gone"), nullptr);
        QVERIFY(reg.commandList().isEmpty());
    }

    void triggerRunsCallbackAndSignal()
    {
        CommandRegistry reg;
        QObject owner;
        Command *a = make(&owner, "a", "Alpha");
        int calls = 0;
        a->setCallback([&calls] { ++calls; });
        QVERIFY(reg.registerCommand(a));
        QSignalSpy triggered(a, &Command::triggered);
        QVERIFY(reg.trigger("a"));
        QCOMPARE(calls, 1);
        QCOMPARE(triggered.count(), 1);
        QVERIFY(!reg.trigger("missing"));
    }

    void disabledCommandDoesNotRun()
    {
        CommandRegistry reg;
        QAbstractItemModelTester tester(&reg);
        QObject owner;
        Command *a = make(&owner, "a", "Alpha");
        int calls = 0;
        a->setCallback([&calls] { ++calls; });
        QVERIFY(reg.registerCommand(a));

        QSignalSpy changed(&reg, &QAbstractItemModel::dataChanged);
        a->setEnabled(false);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(reg.data(reg.index(0), CommandRegistry::EnabledRole).toBool(), false);
        QVERIFY(!reg.trigger("a"));
        QCOMPARE(calls, 0);
        QVERIFY(reg.recents().isEmpty()); // a refused command is not a recent one
        a->setEnabled(true);
        QVERIFY(reg.trigger("a"));
        QCOMPARE(calls, 1);
    }

    void titleChangeUpdatesModel()
    {
        CommandRegistry reg;
        QAbstractItemModelTester tester(&reg);
        QObject owner;
        Command *a = make(&owner, "a", "Settings");
        QVERIFY(reg.registerCommand(a));
        QSignalSpy changed(&reg, &QAbstractItemModel::dataChanged);
        a->setTitle(QStringLiteral("Ajustes")); // what a language switch does
        QCOMPARE(changed.count(), 1);
        QCOMPARE(reg.data(reg.index(0), CommandRegistry::TitleRole).toString(),
                 QStringLiteral("Ajustes"));
    }

    void matchScoreTiers()
    {
        const int prefix = CommandRegistry::matchScore("tog", "Toggle sidebar");
        const int word = CommandRegistry::matchScore("sid", "Toggle sidebar");
        const int subsequence = CommandRegistry::matchScore("tsb", "Toggle sidebar");
        QVERIFY(prefix > word);
        QVERIFY(word > subsequence);
        QVERIFY(subsequence > 0);
        QCOMPARE(CommandRegistry::matchScore("xyz", "Toggle sidebar"), 0);
        QVERIFY(CommandRegistry::matchScore("TOG", "toggle") > 0); // case-insensitive
        QVERIFY(CommandRegistry::matchScore("ab", "ba") == 0); // order matters
        QVERIFY(CommandRegistry::matchScore("go sid", "Go to sidebar") > 0);
    }

    void filterRanksPrefixWordSubsequence()
    {
        CommandRegistry reg;
        QAbstractItemModelTester tester(&reg);
        QObject owner;
        QVERIFY(reg.registerCommand(make(&owner, "sub", "Show hidden items")));
        QVERIFY(reg.registerCommand(make(&owner, "word", "Toggle sidebar")));
        QVERIFY(reg.registerCommand(make(&owner, "prefix", "Sidebar focus")));
        QVERIFY(reg.registerCommand(make(&owner, "none", "Quit")));

        QSignalSpy filterSpy(&reg, &CommandRegistry::filterChanged);
        reg.setFilter("sid");
        QCOMPARE(filterSpy.count(), 1);
        QCOMPARE(ids(reg), (QStringList { "prefix", "word", "sub" }));

        reg.setFilter("");
        QCOMPARE(reg.rowCount(), 4); // registration order, nothing recent yet
        QCOMPARE(ids(reg).first(), QStringLiteral("sub"));

        reg.setFilter("zzz");
        QCOMPARE(reg.rowCount(), 0);
    }

    void categoryIsSearchable()
    {
        CommandRegistry reg;
        QObject owner;
        QVERIFY(reg.registerCommand(make(&owner, "a", "Toggle sidebar", "View")));
        QVERIFY(reg.registerCommand(make(&owner, "b", "Quit", "General")));
        reg.setFilter("view");
        QCOMPARE(ids(reg), (QStringList { "a" }));
        reg.setFilter("view tog"); // category and title together
        QCOMPARE(ids(reg), (QStringList { "a" }));
    }

    void recentsComeFirstWhenQueryIsEmpty()
    {
        CommandRegistry reg;
        QAbstractItemModelTester tester(&reg);
        QObject owner;
        for (const char *id : { "a", "b", "c" })
            QVERIFY(reg.registerCommand(make(&owner, id, QString::fromLatin1(id))));
        QVERIFY(reg.trigger("c"));
        QVERIFY(reg.trigger("b"));
        QCOMPARE(ids(reg), (QStringList { "b", "c", "a" }));
        QVERIFY(reg.trigger("c"));
        QCOMPARE(ids(reg), (QStringList { "c", "b", "a" }));
        // With a query the match wins; recency only breaks ties.
        reg.setFilter("b");
        QCOMPARE(ids(reg), (QStringList { "b" }));
    }

    void recentsPersistAndAreCapped()
    {
        {
            CommandRegistry reg;
            QObject owner;
            for (int i = 0; i < 12; ++i)
                QVERIFY(reg.registerCommand(make(&owner, QString::number(i), QString::number(i))));
            for (int i = 0; i < 12; ++i)
                QVERIFY(reg.trigger(QString::number(i)));
            QCOMPARE(reg.recents().size(), CommandRegistry::kMaxRecents);
            QCOMPARE(reg.recents().first(), QStringLiteral("11"));
        }
        QCOMPARE(QSettings().value("ui/recentCommands").toStringList().size(),
                 CommandRegistry::kMaxRecents);

        CommandRegistry again;
        QCOMPARE(again.recents().size(), CommandRegistry::kMaxRecents);
        QCOMPARE(again.recents().first(), QStringLiteral("11"));
    }

    void tieKeepsRegistrationOrderThenRecency()
    {
        CommandRegistry reg;
        QObject owner;
        QVERIFY(reg.registerCommand(make(&owner, "one", "Open aa")));
        QVERIFY(reg.registerCommand(make(&owner, "two", "Open bb")));
        reg.setFilter("open");
        QCOMPARE(ids(reg), (QStringList { "one", "two" }));
        QVERIFY(reg.trigger("two"));
        QCOMPARE(ids(reg), (QStringList { "two", "one" }));
    }

    void untrackedCommandStaysOutOfRecents()
    {
        CommandRegistry reg;
        QObject owner;
        Command *a = make(&owner, "a", "Alpha");
        a->setTrackRecent(false);
        QVERIFY(reg.registerCommand(a));
        QVERIFY(reg.trigger("a"));
        QVERIFY(reg.recents().isEmpty());
    }

    void shortcutTextIsNative()
    {
        Command c;
        c.setShortcut(QStringLiteral("Ctrl+K"));
        c.setAlternateShortcut(QStringLiteral("Ctrl+Shift+P"));
        QCOMPARE(c.shortcutText(),
                 QKeySequence(QStringLiteral("Ctrl+K")).toString(QKeySequence::NativeText));
        QCOMPARE(c.sequences(), (QStringList { "Ctrl+K", "Ctrl+Shift+P" }));
        Command none;
        QVERIFY(none.shortcutText().isEmpty());
        QVERIFY(none.sequences().isEmpty());
    }
};

QTEST_MAIN(TstCommandRegistry)
#include "tst_command_registry.moc"
