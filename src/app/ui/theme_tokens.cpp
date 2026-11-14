#include "theme_tokens.h"

#include <QCoreApplication>

namespace quarry::ui {

namespace {

constexpr QRgb c(unsigned rgb)
{
    return 0xFF000000U | rgb;
}

constexpr const char *kTokenNames[kTokenCount]
    = { "bg.canvas",     "bg.surface",     "bg.raised",       "bg.hover",
        "bg.pressed",    "fg.default",     "fg.muted",        "fg.disabled",
        "fg.onAccent",   "border.default", "border.strong",   "focus.ring",
        "accent",        "accent.hover",   "danger",          "warning",
        "success",       "info",           "selection.bg",    "selection.fg",
        "grid.null",     "scrim",          "conn.production", "conn.blue",
        "conn.orange",   "conn.teal",      "conn.purple",     "conn.pink",
        "conn.gold",     "conn.green",     "conn.slate",      "syntax.keyword",
        "syntax.string", "syntax.number",  "syntax.comment",  "syntax.function",
        "syntax.type",   "syntax.operator" };

// Red is reserved for danger/production, so it is not offered as a connection color.
constexpr const char *kConnNames[kConnectionColorCount]
    = { QT_TRANSLATE_NOOP("Theme", "Blue"),  QT_TRANSLATE_NOOP("Theme", "Orange"),
        QT_TRANSLATE_NOOP("Theme", "Teal"),  QT_TRANSLATE_NOOP("Theme", "Purple"),
        QT_TRANSLATE_NOOP("Theme", "Pink"),  QT_TRANSLATE_NOOP("Theme", "Gold"),
        QT_TRANSLATE_NOOP("Theme", "Green"), QT_TRANSLATE_NOOP("Theme", "Slate") };

// clang-format off
// Columns follow enum Token. Accent is indigo; ratios are verified by tst_theme_contrast.
constexpr TokenSet kLight = {
    c(0xF3F4F7), c(0xFFFFFF), c(0xFFFFFF), c(0xE6E8EF), c(0xD6D9E3),          // bg: canvas surface raised hover pressed
    c(0x1B1D23), c(0x4B5160), c(0x858A97), c(0xFFFFFF),                       // fg: default muted disabled onAccent
    c(0xC5C9D4), c(0x6F7585), c(0x3730A3),                                    // border default/strong, focus ring
    c(0x4F46E5), c(0x4338CA),                                                 // accent, hover
    c(0xB42318), c(0x8A5A00), c(0x116B38), c(0x0B67A0),                       // danger warning success info
    c(0x4F46E5), c(0xFFFFFF), c(0x5F6575), 0x66000000U,                       // selection bg/fg, grid.null, scrim
    c(0xB42318),                                                              // conn.production
    c(0x2563EB), c(0xC2410C), c(0x0F766E), c(0x7E22CE), c(0xBE185D), c(0x8A6D00), c(0x15803D), c(0x475569),
    c(0x3730A3), c(0x116B38), c(0x8A5A00), c(0x525867), c(0x0B67A0), c(0x7E22CE), c(0x4B5160)}; // syntax: keyword string number comment function type operator

constexpr TokenSet kDark = {
    c(0x14151A), c(0x1B1D24), c(0x242731), c(0x2C303C), c(0x363B4A),
    c(0xE7E9EF), c(0xA8AEBD), c(0x6B7180), c(0x0F1020),
    c(0x3A3F4E), c(0x8A90A3), c(0xA5ABFF),
    c(0x8B93FF), c(0xA3AAFF),
    c(0xFF7B72), c(0xE3B341), c(0x56D364), c(0x58A6FF),
    c(0x3B3F9E), c(0xFFFFFF), c(0x8A90A3), 0x99000000U,
    c(0xFF7B72),
    c(0x5B9DFF), c(0xFB923C), c(0x2DD4BF), c(0xC084FC), c(0xF472B6), c(0xFACC15), c(0x4ADE80), c(0x94A3B8),
    c(0xA5ABFF), c(0x56D364), c(0xE3B341), c(0xA8AEBD), c(0x58A6FF), c(0xC084FC), c(0xC9CEDB)};

constexpr TokenSet kHighContrastLight = {
    c(0xFFFFFF), c(0xFFFFFF), c(0xFFFFFF), c(0xE3E3F5), c(0xCBCBEA),
    c(0x000000), c(0x333333), c(0x595959), c(0xFFFFFF),
    c(0x404040), c(0x000000), c(0x000099),
    c(0x2B2BB0), c(0x1A1A8C),
    c(0xA00000), c(0x6B4000), c(0x005A20), c(0x00468C),
    c(0x000099), c(0xFFFFFF), c(0x333333), 0x99000000U,
    c(0xA00000),
    c(0x0033CC), c(0x8A3000), c(0x005F5F), c(0x5A0099), c(0x990055), c(0x5C4400), c(0x005A20), c(0x404040),
    c(0x000099), c(0x005A20), c(0x6B4000), c(0x333333), c(0x00468C), c(0x5A0099), c(0x000000)};

constexpr TokenSet kHighContrastDark = {
    c(0x000000), c(0x000000), c(0x0A0A0A), c(0x22223D), c(0x38385E),
    c(0xFFFFFF), c(0xE0E0E0), c(0xA0A0A0), c(0x000000),
    c(0xBFBFBF), c(0xFFFFFF), c(0xFFD400),
    c(0xB4B9FF), c(0xD0D3FF),
    c(0xFF8A8A), c(0xFFD400), c(0x6EF08F), c(0x7FC4FF),
    c(0xB4B9FF), c(0x000000), c(0xE0E0E0), 0x99000000U,
    c(0xFF8A8A),
    c(0x7FB0FF), c(0xFFA766), c(0x40E0D0), c(0xD9A6FF), c(0xFF8AC8), c(0xFFE066), c(0x6EF08F), c(0xD0D0D0),
    c(0xB4B9FF), c(0x6EF08F), c(0xFFD400), c(0xE0E0E0), c(0x7FC4FF), c(0xD9A6FF), c(0xFFFFFF)};
// clang-format on

} // namespace

const char *tokenName(Token t)
{
    return kTokenNames[tokenIndex(t)];
}

const char *connectionColorName(int i)
{
    return kConnNames[i];
}

const TokenSet &fixedTokens(ThemeId id)
{
    switch (id) {
    case ThemeId::Light:
        return kLight;
    case ThemeId::Dark:
        return kDark;
    case ThemeId::HighContrastLight:
        return kHighContrastLight;
    case ThemeId::HighContrastDark:
        return kHighContrastDark;
    }
    return kLight;
}

TokenSet tokensFromPalette(const QPalette &p)
{
    const bool dark = p.color(QPalette::Window).lightness() < 128;
    TokenSet t = fixedTokens(dark ? ThemeId::HighContrastDark : ThemeId::HighContrastLight);
    const auto set
        = [&t, &p](Token k, QPalette::ColorRole role) { t[tokenIndex(k)] = p.color(role).rgba(); };
    for (Token k : { Token::BgCanvas, Token::BgSurface, Token::BgRaised })
        set(k, QPalette::Window);
    // No hover/pressed fill exists in HC palettes; controls also change border width for feedback.
    set(Token::BgHover, QPalette::Button);
    set(Token::BgPressed, QPalette::Button);
    for (Token k : { Token::FgDefault, Token::FgMuted, Token::BorderDefault, Token::BorderStrong,
                     Token::GridNull })
        set(k, QPalette::WindowText);
    t[tokenIndex(Token::FgDisabled)]
        = p.color(QPalette::Disabled, QPalette::WindowText).rgba(); // GrayText
    for (Token k : { Token::Accent, Token::AccentHover, Token::SelectionBg, Token::FocusRing })
        set(k, QPalette::Highlight);
    set(Token::FgOnAccent, QPalette::HighlightedText);
    set(Token::SelectionFg, QPalette::HighlightedText);
    set(Token::Info, QPalette::Link);
    // The system palette has no syntax colors: the text color keeps its guaranteed contrast and
    // comments are told apart by italics.
    for (Token k : { Token::SynKeyword, Token::SynString, Token::SynNumber, Token::SynComment,
                     Token::SynFunction, Token::SynType, Token::SynOperator })
        set(k, QPalette::WindowText);
    return t;
}

} // namespace quarry::ui
