#include <QGuiApplication>
#include <QSettings>
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
        QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    }

private:
    QTemporaryDir m_dir;
};

QUICK_TEST_MAIN_WITH_SETUP(tst_controls, Setup)

#include "tst_controls_main.moc"
