#include "ui/theme_tokens.h"

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

using namespace quarry::ui;

namespace {

double channel(int c)
{
    const double s = c / 255.0;
    return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
}

// WCAG 2.x relative luminance.
double luminance(QRgb rgb)
{
    return 0.2126 * channel(qRed(rgb)) + 0.7152 * channel(qGreen(rgb))
        + 0.0722 * channel(qBlue(rgb));
}

double contrast(QRgb a, QRgb b)
{
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

constexpr ThemeId kThemes[]
    = { ThemeId::Light, ThemeId::Dark, ThemeId::HighContrastLight, ThemeId::HighContrastDark };

const char *themeName(ThemeId id)
{
    switch (id) {
    case ThemeId::Light:
        return "Light";
    case ThemeId::Dark:
        return "Dark";
    case ThemeId::HighContrastLight:
        return "HighContrastLight";
    case ThemeId::HighContrastDark:
        return "HighContrastDark";
    }
    return "?";
}

} // namespace

class TstThemeContrast : public QObject
{
    Q_OBJECT

private:
    std::map<std::string, double> m_lowest; // "<category>" -> lowest ratio seen, for the report

    void note(const char *category, ThemeId id, double ratio)
    {
        const std::string key = std::string(category) + " [" + themeName(id) + "]";
        const auto it = m_lowest.find(key);
        if (it == m_lowest.end() || ratio < it->second)
            m_lowest[key] = ratio;
    }

private slots:
    void luminanceKnownPairs()
    {
        QCOMPARE_EQ(std::round(contrast(0xFF000000, 0xFFFFFFFF) * 100) / 100, 21.0);
        QCOMPARE_EQ(std::round(contrast(0xFF777777, 0xFFFFFFFF) * 100) / 100, 4.48);
        QCOMPARE_EQ(std::round(contrast(0xFF767676, 0xFFFFFFFF) * 100) / 100, 4.54);
        QCOMPARE_EQ(contrast(0xFF123456, 0xFF123456), 1.0);
    }

    void tablesAreComplete()
    {
        for (ThemeId id : kThemes) {
            const TokenSet &t = fixedTokens(id);
            for (std::size_t i = 0; i < kTokenCount; ++i) {
                const Token tok = static_cast<Token>(i);
                QVERIFY2(tokenName(tok) != nullptr, "token without a name");
                if (tok != Token::Scrim)
                    QCOMPARE_EQ(t[i] >> 24, 0xFFU); // opaque
            }
            QVERIFY(t[tokenIndex(Token::BgCanvas)] != t[tokenIndex(Token::FgDefault)]);
        }
        for (int i = 0; i < kConnectionColorCount; ++i)
            QVERIFY(qstrlen(connectionColorName(i)) > 0);
    }

    void text_data()
    {
        QTest::addColumn<int>("theme");
        QTest::addColumn<int>("fg");
        QTest::addColumn<int>("bg");
        for (ThemeId id : kThemes) {
            const auto row = [&](Token fg, Token bg) {
                QTest::addRow("%s %s on %s", themeName(id), tokenName(fg), tokenName(bg))
                    << static_cast<int>(id) << static_cast<int>(fg) << static_cast<int>(bg);
            };
            for (Token bg :
                 { Token::BgCanvas, Token::BgSurface, Token::BgRaised, Token::BgHover }) {
                row(Token::FgDefault, bg);
                row(Token::FgMuted, bg);
            }
            row(Token::FgDefault, Token::BgPressed);
            row(Token::FgOnAccent, Token::Accent);
            row(Token::FgOnAccent, Token::AccentHover);
            row(Token::SelectionFg, Token::SelectionBg);
            for (Token fg : { Token::Danger, Token::Warning, Token::Success, Token::Info })
                for (Token bg : { Token::BgSurface, Token::BgCanvas })
                    row(fg, bg);
            row(Token::Danger, Token::BgRaised);
            row(Token::Danger, Token::BgHover);
            row(Token::GridNull, Token::BgSurface);
        }
    }

    void text()
    {
        QFETCH(int, theme);
        QFETCH(int, fg);
        QFETCH(int, bg);
        const TokenSet &t = fixedTokens(static_cast<ThemeId>(theme));
        const double r = contrast(t[static_cast<std::size_t>(fg)], t[static_cast<std::size_t>(bg)]);
        note("text (>= 4.5)", static_cast<ThemeId>(theme), r);
        QVERIFY2(r >= 4.5, qPrintable(QString::number(r, 'f', 2)));
    }

    void indicators_data()
    {
        QTest::addColumn<int>("theme");
        QTest::addColumn<int>("fg");
        QTest::addColumn<int>("bg");
        for (ThemeId id : kThemes) {
            const auto row = [&](Token fg, Token bg) {
                QTest::addRow("%s %s on %s", themeName(id), tokenName(fg), tokenName(bg))
                    << static_cast<int>(id) << static_cast<int>(fg) << static_cast<int>(bg);
            };
            for (Token fg : { Token::FocusRing, Token::BorderStrong, Token::Accent })
                for (Token bg : { Token::BgCanvas, Token::BgSurface })
                    row(fg, bg);
            row(Token::FocusRing, Token::BgRaised);
            row(Token::ConnProduction, Token::BgSurface);
            for (int i = 0; i < kConnectionColorCount; ++i)
                row(connectionToken(i), Token::BgSurface);
        }
    }

    void indicators()
    {
        QFETCH(int, theme);
        QFETCH(int, fg);
        QFETCH(int, bg);
        const TokenSet &t = fixedTokens(static_cast<ThemeId>(theme));
        const double r = contrast(t[static_cast<std::size_t>(fg)], t[static_cast<std::size_t>(bg)]);
        note("indicator (>= 3)", static_cast<ThemeId>(theme), r);
        QVERIFY2(r >= 3.0, qPrintable(QString::number(r, 'f', 2)));
    }

    // WCAG exempts disabled text; we still aim for 3:1 and report when a theme is below it.
    void disabledPreferred()
    {
        for (ThemeId id : kThemes) {
            const TokenSet &t = fixedTokens(id);
            for (Token bg : { Token::BgCanvas, Token::BgSurface }) {
                const double r = contrast(t[tokenIndex(Token::FgDisabled)], t[tokenIndex(bg)]);
                note("fg.disabled (pref >= 3)", id, r);
                if (r < 3.0)
                    qWarning("%s: fg.disabled on %s is %.2f:1 (< 3:1, exempt)", themeName(id),
                             tokenName(bg), r);
            }
        }
    }

    void paletteDerivedHighContrast()
    {
        QPalette p;
        p.setColor(QPalette::Window, QColor(0, 0, 0));
        p.setColor(QPalette::WindowText, QColor(255, 255, 0));
        p.setColor(QPalette::Highlight, QColor(0, 255, 255));
        p.setColor(QPalette::HighlightedText, QColor(0, 0, 0));
        p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0, 255, 0));
        p.setColor(QPalette::Link, QColor(255, 128, 255));
        const TokenSet t = tokensFromPalette(p);
        QCOMPARE(t[tokenIndex(Token::BgCanvas)], QColor(0, 0, 0).rgba());
        QCOMPARE(t[tokenIndex(Token::FgDefault)], QColor(255, 255, 0).rgba());
        QCOMPARE(t[tokenIndex(Token::Accent)], QColor(0, 255, 255).rgba());
        QCOMPARE(t[tokenIndex(Token::FgOnAccent)], QColor(0, 0, 0).rgba());
        QCOMPARE(t[tokenIndex(Token::FgDisabled)], QColor(0, 255, 0).rgba());
        QCOMPARE(t[tokenIndex(Token::Info)], QColor(255, 128, 255).rgba());
        // Status colors come from the fixed dark HC table (the palette is dark).
        QCOMPARE(t[tokenIndex(Token::Danger)],
                 fixedTokens(ThemeId::HighContrastDark)[tokenIndex(Token::Danger)]);
    }

    void cleanupTestCase()
    {
        for (const auto &[key, ratio] : m_lowest)
            qInfo("lowest %s = %.2f", key.c_str(), ratio);
    }
};

QTEST_APPLESS_MAIN(TstThemeContrast)
#include "tst_theme_contrast.moc"
