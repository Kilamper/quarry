#include "theme_manager.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QMetaEnum>
#include <QSettings>
#include <QStyleHints>
#include <QtGui/qaccessibilityhints.h>

#include <algorithm>
#include <cmath>

namespace quarry::ui {

namespace {

constexpr QLatin1StringView kKeyMode { "ui/themeMode" };
constexpr QLatin1StringView kKeyAnimations { "ui/animations" };
constexpr QLatin1StringView kKeyDensity { "ui/density" };
constexpr QLatin1StringView kKeyZoom { "ui/zoom" };

// Windows high-contrast mode recolors the palette; elsewhere HC uses the fixed tables.
#ifdef Q_OS_WIN
constexpr bool kHighContrastFromPalette = true;
#else
constexpr bool kHighContrastFromPalette = false;
#endif

template<class E> E enumFromSetting(const QSettings &s, QLatin1StringView key, E fallback)
{
    bool ok = false;
    const int v
        = QMetaEnum::fromType<E>().keyToValue(s.value(key).toString().toUtf8().constData(), &ok);
    return ok ? static_cast<E>(v) : fallback;
}

template<class E> void saveEnum(QLatin1StringView key, E value)
{
    QSettings().setValue(
        key, QString::fromLatin1(QMetaEnum::fromType<E>().valueToKey(static_cast<int>(value))));
}

} // namespace

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
    const QSettings s;
    m_mode = enumFromSetting(s, kKeyMode, Mode::System);
    m_animations = enumFromSetting(s, kKeyAnimations, Animations::System);
    m_density = enumFromSetting(s, kKeyDensity, Density::Compact);
    m_zoom = std::clamp(s.value(kKeyZoom, 1.0).toDouble(), kMinZoom, kMaxZoom);

    const qreal pt = QGuiApplication::font().pointSizeF();
    m_basePointSize = pt > 0 ? pt : 9.0;
    m_monoFamily = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();

    refresh(); // nothing is connected yet, so no signal reaches anyone
    if (QGuiApplication::instance()) {
        const QStyleHints *hints = QGuiApplication::styleHints();
        connect(hints, &QStyleHints::colorSchemeChanged, this, &ThemeManager::refresh);
        const QAccessibilityHints *a11y = hints->accessibility();
        connect(a11y, &QAccessibilityHints::contrastPreferenceChanged, this,
                &ThemeManager::refresh);
        connect(a11y, &QAccessibilityHints::motionPreferenceChanged, this, &ThemeManager::refresh);
    }
}

ThemeManager::SystemState ThemeManager::systemState() const
{
    if (m_hasOverride)
        return m_override;
    if (!QGuiApplication::instance())
        return {};
    const QStyleHints *hints = QGuiApplication::styleHints();
    return { hints->colorScheme(), hints->accessibility()->contrastPreference(),
             hints->accessibility()->motionPreference() };
}

void ThemeManager::setSystemStateOverride(const SystemState &state)
{
    m_override = state;
    m_hasOverride = true;
    refresh();
}

void ThemeManager::clearSystemStateOverride()
{
    m_hasOverride = false;
    refresh();
}

void ThemeManager::refresh()
{
    const SystemState sys = systemState();
    const bool systemHc
        = m_mode == Mode::System && sys.contrast == Qt::ContrastPreference::HighContrast;
    const bool hc = m_mode == Mode::HighContrast || systemHc;
    const bool fromPalette
        = systemHc && kHighContrastFromPalette && QGuiApplication::instance() != nullptr;

    bool dark = false;
    TokenSet tokens;
    ThemeId id = ThemeId::Light;
    if (fromPalette) {
        tokens = tokensFromPalette(QGuiApplication::palette());
        dark = QColor::fromRgba(tokens[tokenIndex(Token::BgCanvas)]).lightness() < 128;
        id = dark ? ThemeId::HighContrastDark : ThemeId::HighContrastLight;
    } else {
        dark = m_mode == Mode::Dark
            || (m_mode != Mode::Light && sys.colorScheme == Qt::ColorScheme::Dark);
        id = hc ? (dark ? ThemeId::HighContrastDark : ThemeId::HighContrastLight)
                : (dark ? ThemeId::Dark : ThemeId::Light);
        tokens = fixedTokens(id);
    }
    watchPalette(fromPalette);

    const bool reduced = m_animations == Animations::Off
        || (m_animations == Animations::System
            && sys.motion == Qt::MotionPreference::ReducedMotion);

    const bool themeDiffers = tokens != m_tokens || dark != m_dark || hc != m_highContrast;
    m_themeId = id;
    m_tokens = tokens;
    m_dark = dark;
    m_highContrast = hc;
    if (themeDiffers)
        emit themeChanged();

    if (reduced != m_reducedMotion) {
        m_reducedMotion = reduced;
        emit motionChanged();
    }
}

void ThemeManager::watchPalette(bool on)
{
    QCoreApplication *app = QCoreApplication::instance();
    if (on == m_watchingPalette || app == nullptr)
        return;
    m_watchingPalette = on;
    // Installed only in OS high-contrast mode: an application-wide filter sees every event.
    if (on)
        app->installEventFilter(this);
    else
        app->removeEventFilter(this);
}

bool ThemeManager::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ApplicationPaletteChange
        && watched == QCoreApplication::instance())
        refresh();
    return QObject::eventFilter(watched, event);
}

void ThemeManager::setMode(Mode mode)
{
    if (mode == m_mode)
        return;
    m_mode = mode;
    saveEnum(kKeyMode, mode);
    refresh();
    emit modeChanged();
}

void ThemeManager::setAnimations(Animations a)
{
    if (a == m_animations)
        return;
    m_animations = a;
    saveEnum(kKeyAnimations, a);
    refresh();
    emit animationsChanged();
}

void ThemeManager::setDensity(Density d)
{
    if (d == m_density)
        return;
    m_density = d;
    saveEnum(kKeyDensity, d);
    emit densityChanged();
    emit metricsChanged();
}

void ThemeManager::setZoom(qreal zoom)
{
    const qreal z = std::clamp(std::round(zoom * 100) / 100, kMinZoom, kMaxZoom);
    if (qFuzzyCompare(z, m_zoom))
        return;
    m_zoom = z;
    QSettings().setValue(kKeyZoom, z);
    emit zoomChanged();
    emit metricsChanged();
}

int ThemeManager::controlHeight() const
{
    const qreal base = m_density == Density::Compact ? 24 : 32;
    return std::max(kMinTarget, static_cast<int>(std::lround(base * m_zoom)));
}

int ThemeManager::spacing() const
{
    return static_cast<int>(std::lround((m_density == Density::Compact ? 4 : 6) * m_zoom));
}

int ThemeManager::radius() const
{
    return static_cast<int>(std::lround((m_density == Density::Compact ? 4 : 6) * m_zoom));
}

QColor ThemeManager::connectionColor(int i) const
{
    return (i >= 0 && i < kConnectionColorCount) ? color(connectionToken(i)) : QColor();
}

QString ThemeManager::connectionColorName(int i) const
{
    return (i >= 0 && i < kConnectionColorCount)
        ? QCoreApplication::translate("Theme", quarry::ui::connectionColorName(i))
        : QString();
}

} // namespace quarry::ui
