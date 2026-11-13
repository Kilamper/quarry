#pragma once

#include "sqlite_db_state.h"

#include <quarry/provider_api.h>

#include <memory>
#include <optional>
#include <vector>

namespace quarry::sqlite {

// Logical type from a declared column type (SQLite affinity rules, plus the common aliases).
// Empty or unknown declarations give Other: the real type is per cell.
ValueType declaredValueType(const QByteArray &declared);

class SqliteStream : public IResultStream
{
public:
    // Takes ownership of `stmt`.
    SqliteStream(std::shared_ptr<DbState> state, sqlite3_stmt *stmt,
                 std::vector<ColumnInfo> columns);
    ~SqliteStream() override;
    SqliteStream(const SqliteStream &) = delete;
    SqliteStream &operator=(const SqliteStream &) = delete;

    const std::vector<ColumnInfo> &columns() const override { return m_columns; }
    Result<ResultBatch> fetch(qsizetype maxRows, const CancellationToken &token) override;
    bool atEnd() const override { return m_done; }
    std::optional<qint64> affectedRows() const override { return m_affected; }
    QStringList notices() const override { return {}; }
    Result<bool> nextResult(const CancellationToken &) override { return false; }

    // fetch() without opening a CallScope; the caller already holds one.
    Result<ResultBatch> run(qsizetype maxRows, const CancellationToken &token);

private:
    void appendRow(ResultBatch &batch) const;
    void finish();
    Result<ResultBatch> abort(Error error);

    std::shared_ptr<DbState> m_state;
    sqlite3_stmt *m_stmt = nullptr;
    std::vector<ColumnInfo> m_columns;
    std::vector<ValueType> m_declared; // per column, from the declared type
    std::vector<ValueType> m_textType; // type given to TEXT cells of each column
    sqlite3_int64 m_changesBefore = 0;
    bool m_isDml = false;
    bool m_done = false;
    std::optional<qint64> m_affected;
};

} // namespace quarry::sqlite
