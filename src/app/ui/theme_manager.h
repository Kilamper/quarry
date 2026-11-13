#pragma once

#include "theme_tokens.h"

#include <QColor>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace quarry::ui {

// QML singleton `Theme` (this class is registered directly under that name; there is no
// QML wrapper). Single source of truth for colors (theme_tokens.cpp) and metrics.
// Construction only reads QSettings; nothing touches the disk otherwise.
class ThemeManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Theme)
    QML_SINGLETON

    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged FINAL)
    Q_PROPERTY(bool isDark READ isDark NOTIFY themeChanged FINAL)
    Q_PROPERTY(bool isHighContrast READ isHighContrast NOTIFY themeChanged FINAL)

    Q_PROPERTY(
        Animations animations READ animations WRITE setAnimations NOTIFY animationsChanged FINAL)
    Q_PROPERTY(bool reducedMotion READ reducedMotion NOTIFY motionChanged FINAL)
    Q_PROPERTY(int animationDuration READ animationDuration NOTIFY motionChanged FINAL)

    Q_PROPERTY(Density density READ density WRITE setDensity NOTIFY densityChanged FINAL)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY zoomChanged FINAL)
    Q_PROPERTY(qreal fontSize READ fontSize NOTIFY metricsChanged FINAL)
    Q_PROPERTY(QString monoFontFamily READ monoFontFamily CONSTANT FINAL)
    Q_PROPERTY(int controlHeight READ controlHeight NOTIFY metricsChanged FINAL)
    Q_PROPERTY(int spacing READ spacing NOTIFY metricsChanged FINAL)
    Q_PROPERTY(int radius READ radius NOTIFY metricsChanged FINAL)
    Q_PROPERTY(int focusRingWidth READ focusRingWidth CONSTANT FINAL)

    Q_PROPERTY(QColor bgCanvas READ bgCanvas NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor bgSurface READ bgSurface NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor bgRaised READ bgRaised NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor bgHover READ bgHover NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor bgPressed READ bgPressed NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor fgDefault READ fgDefault NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor fgMuted READ fgMuted NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor fgDisabled READ fgDisabled NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor fgOnAccent READ fgOnAccent NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor borderDefault READ borderDefault NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor borderStrong READ borderStrong NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor focusRing READ focusRing NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor accent READ accent NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor accentHover READ accentHover NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor danger READ danger NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor warning READ warning NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor success READ success NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor info READ info NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor selectionBg READ selectionBg NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor selectionFg READ selectionFg NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor gridNull READ gridNull NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor scrim READ scrim NOTIFY themeChanged FINAL)
    Q_PROPERTY(QColor connProduction READ connProduction NOTIFY themeChanged FINAL)
    Q_PROPERTY(int connectionColorCount READ connectionColorCount CONSTANT FINAL)

public:
    enum class Mode { System, Light, Dark, HighContrast };
    Q_ENUM(Mode)
    enum class Density { Compact, Comfortable };
    Q_ENUM(Density)
    enum class Animations { System, On, Off };
    Q_ENUM(Animations)

    // What the OS reports. Tests inject a fixed one with setSystemStateOverride().
    struct SystemState
    {
        Qt::ColorScheme colorScheme = Qt::ColorScheme::Light;
        Qt::ContrastPreference contrast = Qt::ContrastPreference::NoPreference;
        Qt::MotionPreference motion = Qt::MotionPreference::NoPreference;
    };

    static constexpr qreal kMinZoom = 0.8;
    static constexpr qreal kMaxZoom = 2.0;
    static constexpr qreal kZoomStep = 0.1;
    static constexpr int kAnimationMs = 120;
    static constexpr int kMinTarget = 24; // WCAG 2.5.8

    explicit ThemeManager(QObject *parent = nullptr);

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    bool isDark() const { return m_dark; }
    bool isHighContrast() const { return m_highContrast; }

    Animations animations() const { return m_animations; }
    void setAnimations(Animations a);
    bool reducedMotion() const { return m_reducedMotion; }
    int animationDuration() const { return m_reducedMotion ? 0 : kAnimationMs; }

    Density density() const { return m_density; }
    void setDensity(Density d);
    qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    Q_INVOKABLE void zoomIn() { setZoom(m_zoom + kZoomStep); }
    Q_INVOKABLE void zoomOut() { setZoom(m_zoom - kZoomStep); }
    Q_INVOKABLE void resetZoom() { setZoom(1.0); }

    qreal fontSize() const { return m_basePointSize * m_zoom; }
    QString monoFontFamily() const { return m_monoFamily; }
    int controlHeight() const;
    int spacing() const;
    int radius() const;
    static constexpr int focusRingWidth() { return 2; }

    ThemeId themeId() const { return m_themeId; }
    QRgb token(Token t) const { return m_tokens[tokenIndex(t)]; }

    QColor bgCanvas() const { return color(Token::BgCanvas); }
    QColor bgSurface() const { return color(Token::BgSurface); }
    QColor bgRaised() const { return color(Token::BgRaised); }
    QColor bgHover() const { return color(Token::BgHover); }
    QColor bgPressed() const { return color(Token::BgPressed); }
    QColor fgDefault() const { return color(Token::FgDefault); }
    QColor fgMuted() const { return color(Token::FgMuted); }
    QColor fgDisabled() const { return color(Token::FgDisabled); }
    QColor fgOnAccent() const { return color(Token::FgOnAccent); }
    QColor borderDefault() const { return color(Token::BorderDefault); }
    QColor borderStrong() const { return color(Token::BorderStrong); }
    QColor focusRing() const { return color(Token::FocusRing); }
    QColor accent() const { return color(Token::Accent); }
    QColor accentHover() const { return color(Token::AccentHover); }
    QColor danger() const { return color(Token::Danger); }
    QColor warning() const { return color(Token::Warning); }
    QColor success() const { return color(Token::Success); }
    QColor info() const { return color(Token::Info); }
    QColor selectionBg() const { return color(Token::SelectionBg); }
    QColor selectionFg() const { return color(Token::SelectionFg); }
    QColor gridNull() const { return color(Token::GridNull); }
    QColor scrim() const { return color(Token::Scrim); }
    QColor connProduction() const { return color(Token::ConnProduction); }

    static constexpr int connectionColorCount() { return kConnectionColorCount; }
    Q_INVOKABLE QColor connectionColor(int i) const;
    Q_INVOKABLE QString connectionColorName(int i) const;

    void setSystemStateOverride(const SystemState &state);
    void clearSystemStateOverride();

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void modeChanged();
    void themeChanged();
    void animationsChanged();
    void motionChanged();
    void densityChanged();
    void zoomChanged();
    void metricsChanged();

private:
    QColor color(Token t) const { return QColor::fromRgba(m_tokens[tokenIndex(t)]); }
    SystemState systemState() const;
    void refresh();
    void watchPalette(bool on);

    Mode m_mode = Mode::System;
    Animations m_animations = Animations::System;
    Density m_density = Density::Compact;
    qreal m_zoom = 1.0;
    qreal m_basePointSize = 9.0;
    QString m_monoFamily;

    SystemState m_override;
    bool m_hasOverride = false;
    bool m_watchingPalette = false;

    ThemeId m_themeId = ThemeId::Light;
    TokenSet m_tokens = fixedTokens(ThemeId::Light);
    bool m_dark = false;
    bool m_highContrast = false;
    bool m_reducedMotion = false;
};

} // namespace quarry::ui
