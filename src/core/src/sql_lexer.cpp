#include "sql_lexer.h"

namespace quarry {
namespace {

bool isWordStart(QChar c)
{
    return c.isLetter() || c == u'_' || c.isSurrogate();
}

bool isWordPart(QChar c)
{
    return c.isLetterOrNumber() || c == u'_' || c == u'$' || c.isSurrogate();
}

} // namespace

// Position just past the block comment starting at i (nested, as in PostgreSQL).
qsizetype SqlLexer::skipBlockComment(qsizetype i) const
{
    int depth = 0;
    const qsizetype n = m_text.size();
    while (i < n) {
        if (m_text[i] == u'/' && i + 1 < n && m_text[i + 1] == u'*') {
            ++depth;
            i += 2;
        } else if (m_text[i] == u'*' && i + 1 < n && m_text[i + 1] == u'/') {
            --depth;
            i += 2;
            if (depth == 0) {
                return i;
            }
        } else {
            ++i;
        }
    }
    return n;
}

// i is at the opening quote. A doubled closing char is an escape; with backslash, `\x` is too.
qsizetype SqlLexer::skipQuoted(qsizetype i, QChar close, bool backslash) const
{
    const qsizetype n = m_text.size();
    ++i;
    while (i < n) {
        const QChar c = m_text[i];
        if (backslash && c == u'\\') {
            i += 2;
        } else if (c == close) {
            if (i + 1 < n && m_text[i + 1] == close) {
                i += 2;
            } else {
                return i + 1;
            }
        } else {
            ++i;
        }
    }
    return n;
}

// i is at '$'. Returns the position past the whole $tag$...$tag$ body, or -1 if it is not a
// dollar quote (e.g. the parameter `$1`). An unterminated body runs to the end.
qsizetype SqlLexer::dollarQuoteEnd(qsizetype i) const
{
    const qsizetype n = m_text.size();
    qsizetype j = i + 1;
    if (j < n && isWordStart(m_text[j])) {
        while (j < n && m_text[j] != u'$' && isWordPart(m_text[j])) {
            ++j;
        }
    }
    if (j >= n || m_text[j] != u'$') {
        return -1;
    }
    const QStringView tag = m_text.mid(i, j - i + 1);
    const qsizetype end = m_text.indexOf(tag, j + 1);
    return end < 0 ? n : end + tag.size();
}

SqlLexer::Token SqlLexer::next()
{
    const qsizetype n = m_text.size();
    while (m_pos < n) {
        const qsizetype start = m_pos;
        const QChar c = m_text[start];
        const QChar c1 = start + 1 < n ? m_text[start + 1] : QChar();

        if (c.isSpace()) {
            ++m_pos;
        } else if (c == u'-' && c1 == u'-') {
            while (m_pos < n && m_text[m_pos] != u'\n' && m_text[m_pos] != u'\r') {
                ++m_pos;
            }
        } else if (c == u'/' && c1 == u'*') {
            m_pos = skipBlockComment(start);
        } else if (c == u'\'') {
            m_pos = skipQuoted(start, u'\'', false);
            return { Kind::Other, m_text.mid(start, m_pos - start) };
        } else if (c == u'"' || c == u'`') {
            m_pos = skipQuoted(start, c, false);
            return { Kind::Other, m_text.mid(start, m_pos - start) };
        } else if (c == u'[') {
            m_pos = skipQuoted(start, u']', false);
            return { Kind::Other, m_text.mid(start, m_pos - start) };
        } else if (c == u'$') {
            const qsizetype end = dollarQuoteEnd(start);
            m_pos = end < 0 ? start + 1 : end;
            return { Kind::Other, m_text.mid(start, m_pos - start) };
        } else if (c == u'(') {
            ++m_pos;
            return { Kind::Open, m_text.mid(start, 1) };
        } else if (c == u')') {
            ++m_pos;
            return { Kind::Close, m_text.mid(start, 1) };
        } else if (isWordStart(c)) {
            while (m_pos < n && isWordPart(m_text[m_pos])) {
                ++m_pos;
            }
            // PostgreSQL E'...' escape string: the prefix is a lone E glued to the quote.
            if (m_pos - start == 1 && (c == u'E' || c == u'e') && m_pos < n
                && m_text[m_pos] == u'\'') {
                m_pos = skipQuoted(m_pos, u'\'', true);
                return { Kind::Other, m_text.mid(start, m_pos - start) };
            }
            return { Kind::Word, m_text.mid(start, m_pos - start) };
        } else {
            ++m_pos;
            return { Kind::Other, m_text.mid(start, 1) };
        }
    }
    return {};
}

} // namespace quarry
