#pragma once

#include <QStringView>

namespace quarry {

// Generic (not dialect-specific) SQL tokenizer: just enough to find words and parenthesis depth
// while skipping comments, strings, quoted identifiers and dollar-quoted bodies. Tokens are views
// into the original text; nothing is copied.
class SqlLexer
{
public:
    enum class Kind { End, Word, Open, Close, Other };

    struct Token
    {
        Kind kind = Kind::End;
        QStringView text;
    };

    explicit SqlLexer(QStringView text)
        : m_text(text)
    { }

    Token next();

private:
    qsizetype skipBlockComment(qsizetype i) const;
    qsizetype skipQuoted(qsizetype i, QChar close, bool backslash) const;
    qsizetype dollarQuoteEnd(qsizetype i) const;

    QStringView m_text;
    qsizetype m_pos = 0;
};

} // namespace quarry
