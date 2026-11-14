#include "quarry/sql_safety.h"

#include "sql_lexer.h"

namespace quarry {
namespace {

using Kind = SqlLexer::Kind;
using Token = SqlLexer::Token;

bool isWord(const Token &t, QStringView word)
{
    return t.kind == Kind::Word && t.text.compare(word, Qt::CaseInsensitive) == 0;
}

// True if a WHERE appears at parenthesis depth 0 in the rest of the statement.
bool hasTopLevelWhere(SqlLexer &lexer)
{
    int depth = 0;
    for (Token t = lexer.next(); t.kind != Kind::End; t = lexer.next()) {
        if (t.kind == Kind::Open) {
            ++depth;
        } else if (t.kind == Kind::Close) {
            if (depth > 0) {
                --depth;
            }
        } else if (depth == 0 && isWord(t, u"where")) {
            return true;
        }
    }
    return false;
}

// Next depth-0 word that is not part of a CTE definition; CTE bodies sit in parentheses.
Token skipCteToCommand(SqlLexer &lexer)
{
    int depth = 0;
    for (Token t = lexer.next(); t.kind != Kind::End; t = lexer.next()) {
        if (t.kind == Kind::Open) {
            ++depth;
        } else if (t.kind == Kind::Close) {
            if (depth > 0) {
                --depth;
            }
        } else if (depth == 0 && t.kind == Kind::Word
                   && (isWord(t, u"select") || isWord(t, u"insert") || isWord(t, u"update")
                       || isWord(t, u"delete") || isWord(t, u"merge") || isWord(t, u"values")
                       || isWord(t, u"table"))) {
            return t;
        }
    }
    return {};
}

// EXPLAIN without ANALYZE only plans, so it is None. With ANALYZE (bare or in the PostgreSQL
// option list) the statement really runs, so the inner command is classified. shortcut: any
// ANALYZE inside the option list counts, even `ANALYZE false`; a false positive only asks once
// more.
Token skipExplainToCommand(SqlLexer &lexer)
{
    bool analyze = false;
    for (Token t = lexer.next(); t.kind != Kind::End; t = lexer.next()) {
        if (t.kind == Kind::Open) {
            int depth = 1;
            while (depth > 0) {
                const Token inner = lexer.next();
                if (inner.kind == Kind::End) {
                    return {};
                }
                if (inner.kind == Kind::Open) {
                    ++depth;
                } else if (inner.kind == Kind::Close) {
                    --depth;
                } else if (isWord(inner, u"analyze") || isWord(inner, u"analyse")) {
                    analyze = true;
                }
            }
        } else if (isWord(t, u"analyze") || isWord(t, u"analyse")) {
            analyze = true;
        } else if (!isWord(t, u"verbose") && !isWord(t, u"query") && !isWord(t, u"plan")) {
            return analyze ? t : Token {};
        }
    }
    return {};
}

SqlRisk classify(SqlLexer &lexer, Token command, bool production)
{
    if (isWord(command, u"explain")) {
        command = skipExplainToCommand(lexer);
    }
    if (isWord(command, u"with")) {
        command = skipCteToCommand(lexer);
    }
    if (isWord(command, u"delete")) {
        return hasTopLevelWhere(lexer) ? SqlRisk::None : SqlRisk::DeleteWithoutWhere;
    }
    if (isWord(command, u"update")) {
        return hasTopLevelWhere(lexer) ? SqlRisk::None : SqlRisk::UpdateWithoutWhere;
    }
    if (production && isWord(command, u"drop")) {
        return SqlRisk::Drop;
    }
    if (production && isWord(command, u"truncate")) {
        return SqlRisk::Truncate;
    }
    return SqlRisk::None;
}

} // namespace

SqlRisk SqlSafety::check(QStringView statement, bool production)
{
    SqlLexer lexer(statement);
    return classify(lexer, lexer.next(), production);
}

} // namespace quarry
