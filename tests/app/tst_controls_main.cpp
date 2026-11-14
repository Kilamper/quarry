#include "test_screen.h"

#include <QAccessible>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QtQuickTest/quicktest.h>

#include <sqlite3.h>

namespace {

// A small database for the schema tree tests: invoices/items start with the same letter, so
// type-ahead has something to tell apart. Returns false if it could not be written.
bool createSampleDatabase(const QString &file, int extraTables = 0)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(file.toUtf8().constData(), &db) != SQLITE_OK)
        return false;
    const char *sql = "CREATE TABLE customers(id INTEGER PRIMARY KEY, name TEXT NOT NULL);"
                      "CREATE TABLE invoices(id INTEGER PRIMARY KEY, customer_id INTEGER "
                      "REFERENCES customers(id), total REAL);"
                      "CREATE INDEX invoices_customer ON invoices(customer_id);"
                      "CREATE TABLE items(id INTEGER PRIMARY KEY, invoice_id INTEGER);"
                      "CREATE TABLE products(id INTEGER PRIMARY KEY, title TEXT);"
                      "CREATE VIEW totals AS SELECT customer_id, sum(total) FROM invoices;";
    bool ok = sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
    if (ok && extraTables > 0) {
        QByteArray more = "BEGIN;";
        for (int i = 0; i < extraTables; ++i)
            more += "CREATE TABLE big" + QByteArray::number(i) + "(id INTEGER PRIMARY KEY);";
        more += "COMMIT;";
        ok = sqlite3_exec(db, more.constData(), nullptr, nullptr, nullptr) == SQLITE_OK;
    }
    sqlite3_close(db);
    return ok;
}

// What a screen reader bridge gets from an item: the platform-neutral accessibility interface.
class AccessibilityProbe : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    Q_INVOKABLE QVariantMap inspect(QObject *object) const
    {
        QAccessible::setActive(true);
        // Back to inactive on return: an active bridge slows every later model change in the run.
        struct Off
        {
            ~Off() { QAccessible::setActive(false); }
        } off;
        QVariantMap out;
        QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(object);
        if (iface == nullptr)
            return out;
        const QAccessible::State st = iface->state();
        out.insert(QStringLiteral("role"), static_cast<int>(iface->role()));
        out.insert(QStringLiteral("name"), iface->text(QAccessible::Name));
        out.insert(QStringLiteral("description"), iface->text(QAccessible::Description));
        out.insert(QStringLiteral("expandable"), bool(st.expandable));
        out.insert(QStringLiteral("expanded"), bool(st.expanded));
        out.insert(QStringLiteral("collapsed"), bool(st.collapsed));
        out.insert(QStringLiteral("selected"), bool(st.selected));
        out.insert(QStringLiteral("focusable"), bool(st.focusable));
        out.insert(QStringLiteral("focused"), bool(st.focused));
        out.insert(QStringLiteral("childCount"), iface->childCount());
        if (QAccessibleInterface *p = iface->parent())
            out.insert(QStringLiteral("parentRole"), static_cast<int>(p->role()));
        return out;
    }
};

} // namespace

// Keeps the tests away from the real user settings and makes Tab reach every control.
class Setup : public QObject
{
    Q_OBJECT

public slots:
    void applicationAvailable()
    {
        quarry::testing::ScreenPlacer::install();
        QCoreApplication::setOrganizationName(QStringLiteral("QuarryTest"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_controls"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
        // Qt's own file dialog (the fallback offscreen) is built on the Basic style.
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
        QStandardPaths::setTestModeEnabled(true); // never the user's real data directory
        QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
        // The tests read English strings whatever the language of the machine running them.
        QSettings().setValue(QStringLiteral("ui/language"), QStringLiteral("English"));
    }

    // QML reads these: a temporary directory for the connection databases and test files, and
    // the folder of the real SQLite plugin (never the keychain: no test saves a password).
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        engine->rootContext()->setContextProperty(QStringLiteral("testTempDir"), m_work.path());
        engine->rootContext()->setContextProperty(QStringLiteral("testPluginDir"),
                                                  QStringLiteral(QUARRY_PLUGIN_DIR));
        const QString sample = m_work.filePath(QStringLiteral("sample.db"));
        if (!QFileInfo::exists(sample))
            createSampleDatabase(sample);
        engine->rootContext()->setContextProperty(QStringLiteral("testSampleDb"), sample);
        const QString big = m_work.filePath(QStringLiteral("big.db")); // 10 000 tables
        if (!QFileInfo::exists(big))
            createSampleDatabase(big, 10'000);
        engine->rootContext()->setContextProperty(QStringLiteral("testBigDb"), big);
        // The frame budget (PERFORMANCE.md) is for optimized builds; a debug build is several
        // times slower in the QML engine and the TreeView, so it only has to stay responsive.
#ifdef QT_NO_DEBUG
        constexpr int kMaxGapMs = 50;
#else
        constexpr int kMaxGapMs = 5000;
#endif
        engine->rootContext()->setContextProperty(QStringLiteral("testMaxGapMs"), kMaxGapMs);
        // Typing in a 10 000 line file, key press to painted frame (PERFORMANCE.md: p95 16 ms).
#ifdef QT_NO_DEBUG
        constexpr int kMaxKeystrokeMs = 16;
#else
        constexpr int kMaxKeystrokeMs = 1000;
#endif
        engine->rootContext()->setContextProperty(QStringLiteral("testMaxKeystrokeMs"),
                                                  kMaxKeystrokeMs);
        engine->rootContext()->setContextProperty(QStringLiteral("a11yProbe"),
                                                  new AccessibilityProbe(engine));
    }

private:
    QTemporaryDir m_dir;
    QTemporaryDir m_work;
};

QUICK_TEST_MAIN_WITH_SETUP(tst_controls, Setup)

#include "tst_controls_main.moc"
