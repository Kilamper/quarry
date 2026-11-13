#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QtQuickTest/quicktest.h>

// Keeps the tests away from the real user settings and makes Tab reach every control.
class Setup : public QObject
{
    Q_OBJECT

public slots:
    void applicationAvailable()
    {
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
    }

private:
    QTemporaryDir m_dir;
    QTemporaryDir m_work;
};

QUICK_TEST_MAIN_WITH_SETUP(tst_controls, Setup)

#include "tst_controls_main.moc"
