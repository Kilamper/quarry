#include "ui/language_manager.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace quarry::ui;
using Choice = LanguageManager::Choice;

class TstLanguageManager : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("QuarryTest"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_language_manager"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
    }

    void init() { QSettings().clear(); }

    void resolveMapsSystemToFirstSupportedLanguage()
    {
        QCOMPARE(LanguageManager::resolve(Choice::English, { "es-ES" }), Choice::English);
        QCOMPARE(LanguageManager::resolve(Choice::Spanish, { "en-US" }), Choice::Spanish);
        QCOMPARE(LanguageManager::resolve(Choice::System, { "es-MX", "en-US" }), Choice::Spanish);
        QCOMPARE(LanguageManager::resolve(Choice::System, { "en-GB", "es" }), Choice::English);
        QCOMPARE(LanguageManager::resolve(Choice::System, { "de-DE", "es-ES" }), Choice::Spanish);
        QCOMPARE(LanguageManager::resolve(Choice::System, { "de-DE", "fr" }), Choice::English);
        QCOMPARE(LanguageManager::resolve(Choice::System, {}), Choice::English);
    }

    void switchingInstallsAndRemovesTheTranslator()
    {
        LanguageManager lm(nullptr);
        lm.setChoice(Choice::English);
        QCOMPARE(lm.effective(), Choice::English);
        QCOMPARE(QCoreApplication::translate("MainWindow", "Toggle sidebar"),
                 QStringLiteral("Toggle sidebar"));

        QSignalSpy changed(&lm, &LanguageManager::choiceChanged);
        lm.setChoice(Choice::Spanish);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(lm.effective(), Choice::Spanish);
        QCOMPARE(QCoreApplication::translate("MainWindow", "Toggle sidebar"),
                 QStringLiteral("Mostrar u ocultar la barra lateral"));

        lm.setChoice(Choice::English);
        QCOMPARE(QCoreApplication::translate("MainWindow", "Toggle sidebar"),
                 QStringLiteral("Toggle sidebar"));
    }

    void choiceIsPersistedAndRestored()
    {
        {
            LanguageManager lm(nullptr);
            lm.setChoice(Choice::Spanish);
        }
        QCOMPARE(QSettings().value("ui/language").toString(), QStringLiteral("Spanish"));
        LanguageManager again(nullptr);
        QCOMPARE(again.choice(), Choice::Spanish);
        QCOMPARE(again.effective(), Choice::Spanish);
        QCOMPARE(QCoreApplication::translate("MainWindow", "Settings"), QStringLiteral("Ajustes"));
    }
};

QTEST_MAIN(TstLanguageManager)
#include "tst_language_manager.moc"
