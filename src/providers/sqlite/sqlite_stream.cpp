#include "sqlite_stream.h"

#include "sqlite_errors.h"
#include "sqlite_text.h"

#include <cstring>

namespace quarry::sqlite {

namespace {

constexpr qsizetype kDefaultFetch = 500;

bool startsWithDml(const char *sql)
{
    if (!sql)
        return false;
    const char *p = sql;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            ++p;
        if (p[0] == '-' && p[1] == '-') {
            while (*p && *p != '\n')
                ++p;
        } else if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/'))
                ++p;
            if (*p)
                p += 2;
        } else {
            break;
        }
    }
    for (const char *keyword : { "insert", "update", "delete", "replace" }) {
        const size_t len = std::strlen(keyword);
        if (sqlite3_strnicmp(p, keyword, static_cast<int>(len)) == 0
            && !(p[len] >= 'a' && p[len] <= 'z') && !(p[len] >= 'A' && p[len] <= 'Z'))
            return true;
    }
    return false;
}

bool isTextual(ValueType type)
{
    return type == ValueType::Text || type == ValueType::Decimal || type == ValueType::Date
        || type == ValueType::Time || type == ValueType::DateTime || type == ValueType::Json;
}

} // namespace

ValueType declaredValueType(const QByteArray &declared)
{
    const QByteArray d = declared.toUpper();
    if (d.isEmpty())
        return ValueType::Other;
    if (d.contains("BOOL"))
        return ValueType::Bool;
    if (d.contains("INT"))
        return ValueType::Int64;
    if (d.contains("CHAR") || d.contains("CLOB") || d.contains("TEXT"))
        return ValueType::Text;
    if (d.contains("BLOB"))
        return ValueType::Bytes;
    if (d.contains("REAL") || d.contains("FLOA") || d.contains("DOUB"))
        return ValueType::Double;
    if (d.contains("JSON"))
        return ValueType::Json;
    if (d.contains("DATE") || d.contains("TIME"))
        return ValueType::DateTime;
    if (d.contains("NUMERIC") || d.contains("DECIMAL"))
        return ValueType::Decimal;
    return ValueType::Other;
}

SqliteStream::SqliteStream(std::shared_ptr<DbState> state, sqlite3_stmt *stmt,
                           std::vector<ColumnInfo> columns)
    : m_state(std::move(state))
    , m_stmt(stmt)
    , m_columns(std::move(columns))
{
    for (const auto &column : m_columns) {
        m_declared.push_back(column.type);
        m_textType.push_back(isTextual(column.type) ? column.type : ValueType::Text);
    }
    if (!m_stmt) {
        m_done = true;
        return;
    }
    m_changesBefore = sqlite3_total_changes64(m_state->db);
    m_isDml = startsWithDml(sqlite3_sql(m_stmt));
}

SqliteStream::~SqliteStream()
{
    sqlite3_finalize(m_stmt);
}

Result<ResultBatch> SqliteStream::fetch(qsizetype maxRows, const CancellationToken &token)
{
    CallScope scope(*m_state);
    return run(maxRows, token);
}

Result<ResultBatch> SqliteStream::abort(Error error)
{
    sqlite3_finalize(m_stmt);
    m_stmt = nullptr;
    m_done = true;
    return fail(std::move(error));
}

void SqliteStream::finish()
{
    const sqlite3_int64 total = sqlite3_total_changes64(m_state->db);
    if (m_isDml || total != m_changesBefore)
        m_affected = sqlite3_changes64(m_state->db);
    sqlite3_finalize(m_stmt);
    m_stmt = nullptr;
    m_done = true;
}

Result<ResultBatch> SqliteStream::run(qsizetype maxRows, const CancellationToken &token)
{
    ResultBatch batch;
    batch.columns.resize(m_columns.size());
    if (m_done)
        return batch;
    if (!m_state->db)
        return abort(
            Error { ErrorKind::Connection, Text::tr("The connection was closed."), {}, {}, {} });
    if (maxRows <= 0)
        maxRows = kDefaultFetch;
    for (auto &column : batch.columns)
        column.reserve(maxRows);

    qsizetype rows = 0;
    while (rows < maxRows) {
        if (token.isCancelled() || m_state->cancel.load(std::memory_order_acquire))
            return abort(cancelledError());
        const int rc = sqlite3_step(m_stmt);
        if (rc == SQLITE_ROW) {
            appendRow(batch);
            ++rows;
        } else if (rc == SQLITE_DONE) {
            finish();
            break;
        } else {
            return abort(makeError(m_state->db, rc));
        }
    }
    // An interrupt that the statement never noticed would stay armed for the next fetch.
    if (!m_done && m_state->wasInterrupted())
        return abort(cancelledError());

    batch.rowCount = rows;
    for (size_t i = 0; i < m_columns.size(); ++i) {
        if (batch.columns[i].isMixed())
            m_columns[i].type = ValueType::Other;
    }
    return batch;
}

void SqliteStream::appendRow(ResultBatch &batch) const
{
    const int count = static_cast<int>(m_columns.size());
    for (int i = 0; i < count; ++i) {
        ColumnBuffer &out = batch.columns[static_cast<size_t>(i)];
        const ValueType declared = m_declared[static_cast<size_t>(i)];
        switch (sqlite3_column_type(m_stmt, i)) {
        case SQLITE_INTEGER:
        case SQLITE_FLOAT: {
            const bool isInt = sqlite3_column_type(m_stmt, i) == SQLITE_INTEGER;
            if (declared == ValueType::Decimal) { // keep exact text, like the declared type
                const auto *text = reinterpret_cast<const char *>(sqlite3_column_text(m_stmt, i));
                out.appendBytes(QByteArrayView(text, sqlite3_column_bytes(m_stmt, i)),
                                ValueType::Decimal);
            } else if (isInt) {
                const sqlite3_int64 v = sqlite3_column_int64(m_stmt, i);
                if (declared == ValueType::Bool && (v == 0 || v == 1))
                    out.appendBool(v == 1);
                else
                    out.append(static_cast<int64_t>(v));
            } else {
                out.append(sqlite3_column_double(m_stmt, i));
            }
            break;
        }
        case SQLITE_TEXT: {
            const auto *text = reinterpret_cast<const char *>(sqlite3_column_text(m_stmt, i));
            out.appendBytes(QByteArrayView(text, sqlite3_column_bytes(m_stmt, i)),
                            m_textType[static_cast<size_t>(i)]);
            break;
        }
        case SQLITE_BLOB: {
            const auto *blob = static_cast<const char *>(sqlite3_column_blob(m_stmt, i));
            const int size = sqlite3_column_bytes(m_stmt, i);
            out.appendBytes(blob ? QByteArrayView(blob, size) : QByteArrayView(), ValueType::Bytes);
            break;
        }
        default:
            out.appendNull();
            break;
        }
    }
}

} // namespace quarry::sqlite
