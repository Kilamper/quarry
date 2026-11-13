#include "ui/theme_manager.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace quarry::ui;
using Mode = ThemeManager::Mode;

class TstThemeManager : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    static ThemeManager::SystemState
    state(Qt::ColorScheme scheme,
          Qt::ContrastPreference contrast = Qt::ContrastPreference::NoPreference,
          Qt::MotionPreference motion = Qt::MotionPreference::NoPreference)
    {
        return { scheme, contrast, motion };
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("QuarryTest"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_theme_manager"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
    }

    void init() { QSettings().clear(); }

    void defaults()
    {
        ThemeManager tm;
        QCOMPARE(tm.mode(), Mode::System);
        QCOMPARE(tm.density(), ThemeManager::Density::Compact);
        QCOMPARE(tm.animations(), ThemeManager::Animations::System);
        QCOMPARE(tm.zoom(), 1.0);
        QCOMPARE(tm.focusRingWidth(), 2);
        QVERIFY(!tm.monoFontFamily().isEmpty());
        QCOMPARE(tm.connectionColorCount(), 8);
        QVERIFY(tm.connectionColor(0).isValid());
        QVERIFY(!tm.connectionColor(8).isValid());
        QVERIFY(!tm.connectionColorName(2).isEmpty());
    }

    void modeSwitching()
    {
        ThemeManager tm;
        tm.setSystemStateOverride(state(Qt::ColorScheme::Dark));
        QVERIFY(tm.isDark()); // System follows the OS
        QVERIFY(!tm.isHighContrast());
        QCOMPARE(tm.themeId(), ThemeId::Dark);

        QSignalSpy theme(&tm, &ThemeManager::themeChanged);
        tm.setMode(Mode::Light);
        QVERIFY(!tm.isDark());
        QCOMPARE(tm.themeId(), ThemeId::Light);
        QCOMPARE(theme.count(), 1);
        QCOMPARE(tm.bgCanvas(),
                 QColor::fromRgba(fixedTokens(ThemeId::Light)[tokenIndex(Token::BgCanvas)]));

        tm.setMode(Mode::Dark);
        QCOMPARE(tm.themeId(), ThemeId::Dark);

        tm.setMode(Mode::HighContrast); // forced HC keeps the OS light/dark preference
        QVERIFY(tm.isHighContrast());
        QCOMPARE(tm.themeId(), ThemeId::HighContrastDark);
        tm.setSystemStateOverride(state(Qt::ColorScheme::Light));
        QCOMPARE(tm.themeId(), ThemeId::HighContrastLight);

        tm.setMode(Mode::Light);
        QCOMPARE(tm.themeId(), ThemeId::Light);
    }

    void followsSystemLive()
    {
        ThemeManager tm;
        tm.setSystemStateOverride(state(Qt::ColorScheme::Light));
        QSignalSpy theme(&tm, &ThemeManager::themeChanged);
        tm.setSystemStateOverride(state(Qt::ColorScheme::Dark));
        QCOMPARE(theme.count(), 1);
        QVERIFY(tm.isDark());
        tm.setSystemStateOverride(state(Qt::ColorScheme::Dark)); // unchanged: no signal
        QCOMPARE(theme.count(), 1);
    }

    void systemHighContrast()
    {
        ThemeManager tm;
        tm.setSystemStateOverride(
            state(Qt::ColorScheme::Light, Qt::ContrastPreference::HighContrast));
        QVERIFY(tm.isHighContrast());
#ifndef Q_OS_WIN // on Windows the colors come from the OS palette instead
        QCOMPARE(tm.themeId(), ThemeId::HighContrastLight);
#endif
        tm.setMode(Mode::Light); // an explicit choice beats the OS
        QVERIFY(!tm.isHighContrast());
        QCOMPARE(tm.themeId(), ThemeId::Light);
    }

    void persistence()
    {
        {
            ThemeManager tm;
            tm.setMode(Mode::Dark);
            tm.setDensity(ThemeManager::Density::Comfortable);
            tm.setAnimations(ThemeManager::Animations::Off);
            tm.setZoom(1.5);
        }
        QSettings s;
        QCOMPARE(s.value(QStringLiteral("ui/themeMode")).toString(), QStringLiteral("Dark"));
        QCOMPARE(s.value(QStringLiteral("ui/density")).toString(), QStringLiteral("Comfortable"));
        QCOMPARE(s.value(QStringLiteral("ui/animations")).toString(), QStringLiteral("Off"));
        QCOMPARE(s.value(QStringLiteral("ui/zoom")).toDouble(), 1.5);

        ThemeManager again;
        QCOMPARE(again.mode(), Mode::Dark);
        QCOMPARE(again.density(), ThemeManager::Density::Comfortable);
        QCOMPARE(again.animations(), ThemeManager::Animations::Off);
        QCOMPARE(again.zoom(), 1.5);
    }

    void badSettingsFallBack()
    {
        QSettings s;
        s.setValue(QStringLiteral("ui/themeMode"), QStringLiteral("Neon"));
        s.setValue(QStringLiteral("ui/zoom"), 9.0);
        s.sync();
        ThemeManager tm;
        QCOMPARE(tm.mode(), Mode::System);
        QCOMPARE(tm.zoom(), ThemeManager::kMaxZoom);
    }

    void zoomClampsAndSteps()
    {
        ThemeManager tm;
        tm.setZoom(5);
        QCOMPARE(tm.zoom(), 2.0);
        tm.setZoom(0.1);
        QCOMPARE(tm.zoom(), 0.8);
        tm.resetZoom();
        QCOMPARE(tm.zoom(), 1.0);
        QSignalSpy metrics(&tm, &ThemeManager::metricsChanged);
        tm.zoomIn();
        QCOMPARE(tm.zoom(), 1.1);
        QCOMPARE(metrics.count(), 1);
        for (int i = 0; i < 20; ++i)
            tm.zoomIn();
        QCOMPARE(tm.zoom(), 2.0);
        tm.zoomIn(); // already at the limit: no signal
        QCOMPARE(metrics.count(), 10);
        for (int i = 0; i < 30; ++i)
            tm.zoomOut();
        QCOMPARE(tm.zoom(), 0.8);
    }

    void densityMetrics()
    {
        ThemeManager tm;
        for (const qreal zoom : { 0.8, 1.0, 1.5, 2.0 }) {
            tm.setZoom(zoom);
            tm.setDensity(ThemeManager::Density::Compact);
            const int compact = tm.controlHeight();
            tm.setDensity(ThemeManager::Density::Comfortable);
            const int comfortable = tm.controlHeight();
            QVERIFY2(compact >= 24, qPrintable(QString::number(compact)));
            QVERIFY(comfortable >= 24);
            QVERIFY(comfortable > compact || zoom < 1.0);
        }
        tm.setZoom(1.0);
        tm.setDensity(ThemeManager::Density::Compact);
        QCOMPARE(tm.controlHeight(), 24);
        tm.setDensity(ThemeManager::Density::Comfortable);
        QCOMPARE(tm.controlHeight(), 32);
        tm.setZoom(2.0);
        QCOMPARE(tm.controlHeight(), 64);
        const qreal doubled = tm.fontSize();
        tm.setZoom(1.0);
        QVERIFY(tm.fontSize() > 0);
        QCOMPARE(doubled, tm.fontSize() * 2);
        tm.setZoom(0.8);
        QVERIFY(tm.spacing() >= 1);
    }

    void reducedMotion()
    {
        ThemeManager tm;
        tm.setSystemStateOverride(state(Qt::ColorScheme::Light));
        QVERIFY(!tm.reducedMotion());
        QCOMPARE(tm.animationDuration(), ThemeManager::kAnimationMs);

        QSignalSpy motion(&tm, &ThemeManager::motionChanged);
        tm.setSystemStateOverride(state(Qt::ColorScheme::Light,
                                        Qt::ContrastPreference::NoPreference,
                                        Qt::MotionPreference::ReducedMotion));
        QVERIFY(tm.reducedMotion());
        QCOMPARE(tm.animationDuration(), 0);
        QCOMPARE(motion.count(), 1);

        tm.setAnimations(ThemeManager::Animations::On); // user override beats the OS
        QVERIFY(!tm.reducedMotion());
        QCOMPARE(tm.animationDuration(), ThemeManager::kAnimationMs);

        tm.setSystemStateOverride(state(Qt::ColorScheme::Light));
        tm.setAnimations(ThemeManager::Animations::Off);
        QVERIFY(tm.reducedMotion());
        QCOMPARE(tm.animationDuration(), 0);
    }
};

QTEST_MAIN(TstThemeManager)
#include "tst_theme_manager.moc"
