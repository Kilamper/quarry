#pragma once

#include <QPalette>
#include <QRgb>

#include <array>
#include <cstddef>

namespace quarry::ui {

enum class ThemeId { Light, Dark, HighContrastLight, HighContrastDark };

// Semantic color tokens. Order is the column order of the tables in theme_tokens.cpp.
enum class Token : std::size_t {
    BgCanvas,
    BgSurface,
    BgRaised,
    BgHover,
    BgPressed,
    FgDefault,
    FgMuted,
    FgDisabled,
    FgOnAccent,
    BorderDefault,
    BorderStrong,
    FocusRing,
    Accent,
    AccentHover,
    Danger,
    Warning,
    Success,
    Info,
    SelectionBg,
    SelectionFg,
    GridNull,
    Scrim,
    ConnProduction,
    Conn0,
    Conn1,
    Conn2,
    Conn3,
    Conn4,
    Conn5,
    Conn6,
    Conn7,
    // Syntax highlighting of the SQL editor (UX.md section 3). Each one is >= 4.5:1 on bg.canvas
    // and bg.hover (the current-statement band); tst_theme_contrast checks it.
    SynKeyword,
    SynString,
    SynNumber,
    SynComment,
    SynFunction,
    SynType,
    SynOperator,
    Count
};

inline constexpr std::size_t kTokenCount = static_cast<std::size_t>(Token::Count);
inline constexpr int kConnectionColorCount = 8;

using TokenSet = std::array<QRgb, kTokenCount>;

constexpr std::size_t tokenIndex(Token t)
{
    return static_cast<std::size_t>(t);
}

constexpr Token connectionToken(int i)
{
    return static_cast<Token>(static_cast<std::size_t>(Token::Conn0) + static_cast<std::size_t>(i));
}

// Dotted name used in docs and test messages, e.g. "bg.canvas".
const char *tokenName(Token t);

// Untranslated English name of connection color i (0..7); translate with
// QCoreApplication::translate("Theme", name).
const char *connectionColorName(int i);

// Colors are ARGB; every token is opaque except Scrim.
const TokenSet &fixedTokens(ThemeId id);

// High-contrast tokens derived from the OS palette (Windows HC mode, UX.md section 6).
// Status colors (danger/warning/success) have no system equivalent and come from the fixed table.
TokenSet tokensFromPalette(const QPalette &palette);

} // namespace quarry::ui
