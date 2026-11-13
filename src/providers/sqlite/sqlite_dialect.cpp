#include "sqlite_dialect.h"

#include <sqlite3.h>

namespace quarry::sqlite {

namespace {

QString quoteWith(QStringView text, QChar quote)
{
    QString out;
    out.reserve(text.size() + 2);
    out += quote;
    for (const QChar c : text) {
        if (c == quote)
            out += quote;
        out += c;
    }
    out += quote;
    return out;
}

bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('$');
}

// Tracks just enough of the grammar to know when a ';' ends a statement. Only
// CREATE [TEMP] TRIGGER ... BEGIN ... END needs it: its body holds ';' of its own.
class StatementScanner
{
public:
    void word(const QString &lower)
    {
        if (m_words < 3)
            m_first[m_words] = lower;
        ++m_words;
        if (m_words == 3 || (m_words == 2 && m_first[1] == QLatin1String("trigger")))
            m_trigger = m_first[0] == QLatin1String("create")
                && (m_first[1] == QLatin1String("trigger")
                    || ((m_first[1] == QLatin1String("temp")
                         || m_first[1] == QLatin1String("temporary"))
                        && m_first[2] == QLatin1String("trigger")));
        m_endPending = false;
        if (!m_trigger)
            return;
        if (!m_inBody) {
            m_inBody = lower == QLatin1String("begin");
        } else if (lower == QLatin1String("case")) {
            ++m_caseDepth;
        } else if (lower == QLatin1String("end")) {
            if (m_caseDepth > 0)
                --m_caseDepth;
            else
                m_endPending = true;
        }
    }
    void other() { m_endPending = false; }
    bool terminatorEndsStatement() const { return !m_trigger || !m_inBody || m_endPending; }
    void reset() { *this = StatementScanner(); }

private:
    QString m_first[3];
    int m_words = 0;
    bool m_trigger = false;
    bool m_inBody = false;
    bool m_endPending = false;
    int m_caseDepth = 0;
};

} // namespace

QString SqliteDialect::quoteIdentifier(QStringView name) const
{
    return quoteWith(name, QLatin1Char('"'));
}

QString SqliteDialect::quoteLiteral(QStringView text) const
{
    return quoteWith(text, QLatin1Char('\''));
}

QString SqliteDialect::highlightingDefinition() const
{
    // KSyntaxHighlighting ships no dedicated SQLite definition.
    return QStringLiteral("SQL");
}

QStringList SqliteDialect::keywords() const
{
    QStringList out;
    const int count = sqlite3_keyword_count();
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        const char *name = nullptr;
        int length = 0;
        if (sqlite3_keyword_name(i, &name, &length) == SQLITE_OK)
            out.push_back(QString::fromLatin1(name, length));
    }
    return out;
}

std::vector<StatementRange> SqliteDialect::splitStatements(QStringView script) const
{
    std::vector<StatementRange> out;
    const qsizetype n = script.size();
    qsizetype start = 0;
    bool hasToken = false;
    StatementScanner scanner;

    auto flush = [&](qsizetype end) {
        if (hasToken) {
            qsizetype b = start;
            qsizetype e = end;
            while (b < e && script[b].isSpace())
                ++b;
            while (e > b && script[e - 1].isSpace())
                --e;
            out.push_back({ b, e });
        }
        hasToken = false;
        scanner.reset();
    };

    // Skips a quoted run starting at `i` (script[i] is the opening quote); returns the index
    // after the closing quote, or n if unterminated. A doubled quote is an escaped quote.
    auto skipQuoted = [&](qsizetype i, QChar close, bool doubling) {
        ++i;
        while (i < n) {
            if (script[i] == close) {
                if (doubling && i + 1 < n && script[i + 1] == close) {
                    i += 2;
                    continue;
                }
                return i + 1;
            }
            ++i;
        }
        return n;
    };

    qsizetype i = 0;
    while (i < n) {
        const QChar c = script[i];
        if (c.isSpace()) {
            ++i;
        } else if (c == QLatin1Char('-') && i + 1 < n && script[i + 1] == QLatin1Char('-')) {
            while (i < n && script[i] != QLatin1Char('\n'))
                ++i;
        } else if (c == QLatin1Char('/') && i + 1 < n && script[i + 1] == QLatin1Char('*')) {
            const qsizetype close = script.indexOf(QLatin1String("*/"), i + 2);
            i = close < 0 ? n : close + 2;
        } else if (c == QLatin1Char(';')) {
            if (scanner.terminatorEndsStatement()) {
                flush(i);
                start = i + 1;
            } else {
                scanner.other();
            }
            ++i;
        } else {
            hasToken = true;
            if (c == QLatin1Char('\'') || c == QLatin1Char('"') || c == QLatin1Char('`')) {
                i = skipQuoted(i, c, true);
                scanner.other();
            } else if (c == QLatin1Char('[')) {
                i = skipQuoted(i, QLatin1Char(']'), false);
                scanner.other();
            } else if (isWordChar(c)) {
                const qsizetype wordStart = i;
                while (i < n && isWordChar(script[i]))
                    ++i;
                scanner.word(script.mid(wordStart, i - wordStart).toString().toLower());
            } else {
                scanner.other();
                ++i;
            }
        }
    }
    flush(n);
    return out;
}

QString SqliteDialect::limitClause(qint64 limit, qint64 offset) const
{
    QString out;
    if (limit >= 0)
        out = QStringLiteral("LIMIT %1").arg(limit);
    else if (offset > 0)
        out = QStringLiteral("LIMIT -1"); // OFFSET needs a LIMIT in SQLite
    if (offset > 0)
        out += QStringLiteral(" OFFSET %1").arg(offset);
    return out;
}

QString SqliteDialect::explainPrefix(ExplainMode mode) const
{
    // SQLite has no EXPLAIN ANALYZE: the empty prefix means "run the statement as is".
    return mode == ExplainMode::Plan ? QStringLiteral("EXPLAIN QUERY PLAN ") : QString();
}

} // namespace quarry::sqlite
