#include "sqlite_connection.h"

#include "sqlite_errors.h"
#include "sqlite_stream.h"
#include "sqlite_text.h"

#include <QDate>
#include <QDateTime>
#include <QFileInfo>

#include <cstring>

namespace quarry::sqlite {

namespace {

Error notOpenError()
{
    return Error { ErrorKind::Connection, Text::tr("The connection is not open."), {}, {}, {} };
}

// True if only whitespace, comments and ';' remain: the statement text had no second statement.
bool tailIsBlank(const char *p)
{
    while (*p) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f' || *p == ';') {
            ++p;
        } else if (p[0] == '-' && p[1] == '-') {
            while (*p && *p != '\n')
                ++p;
        } else if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/'))
                ++p;
            if (*p)
                p += 2;
        } else {
            return false;
        }
    }
    return true;
}

int bindValue(sqlite3_stmt *stmt, int index, const QVariant &value)
{
    if (value.isNull()) // includes invalid and typed-null variants
        return sqlite3_bind_null(stmt, index);
    switch (value.metaType().id()) {
    case QMetaType::Bool:
        return sqlite3_bind_int64(stmt, index, value.toBool() ? 1 : 0);
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::LongLong:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::UChar:
        return sqlite3_bind_int64(stmt, index, value.toLongLong());
    case QMetaType::ULong:
    case QMetaType::ULongLong: {
        const qulonglong u = value.toULongLong();
        if (u <= static_cast<qulonglong>(INT64_MAX))
            return sqlite3_bind_int64(stmt, index, static_cast<sqlite3_int64>(u));
        const QByteArray text = QByteArray::number(u); // does not fit int64: keep it exact
        return sqlite3_bind_text(stmt, index, text.constData(), static_cast<int>(text.size()),
                                 SQLITE_TRANSIENT);
    }
    case QMetaType::Double:
    case QMetaType::Float:
        return sqlite3_bind_double(stmt, index, value.toDouble());
    case QMetaType::QByteArray: {
        const QByteArray bytes = value.toByteArray();
        if (bytes.isEmpty())
            return sqlite3_bind_zeroblob(stmt, index, 0);
        return sqlite3_bind_blob64(stmt, index, bytes.constData(),
                                   static_cast<sqlite3_uint64>(bytes.size()), SQLITE_TRANSIENT);
    }
    default: {
        QString text;
        if (value.metaType().id() == QMetaType::QDateTime)
            text = value.toDateTime().toString(Qt::ISODateWithMs);
        else if (value.metaType().id() == QMetaType::QDate)
            text = value.toDate().toString(Qt::ISODate);
        else
            text = value.toString();
        const QByteArray utf8 = text.toUtf8();
        return sqlite3_bind_text64(stmt, index, utf8.constData(),
                                   static_cast<sqlite3_uint64>(utf8.size()), SQLITE_TRANSIENT,
                                   SQLITE_UTF8);
    }
    }
}

} // namespace

SqliteConnection::SqliteConnection(ConnectionConfig config)
    : m_config(std::move(config))
    , m_state(std::make_shared<DbState>())
{ }

SqliteConnection::~SqliteConnection()
{
    closeDb();
}

Result<ServerInfo> SqliteConnection::open(const CancellationToken &token)
{
    CallScope scope(*m_state);
    if (m_state->db)
        return fail(
            Error { ErrorKind::Internal, Text::tr("The connection is already open."), {}, {}, {} });
    if (token.isCancelled())
        return fail(cancelledError());

    const QString file = m_config.values.value(QStringLiteral("file")).toString();
    const bool readOnly = m_config.values.value(QStringLiteral("readOnly")).toBool();
    const int busyMs = m_config.values.value(QStringLiteral("busyTimeoutMs"), 5000).toInt();
    if (file.isEmpty())
        return fail(Error { ErrorKind::Connection,
                            Text::tr("Choose a database file, or use :memory:."),
                            {},
                            {},
                            {} });

    const bool special
        = file == QLatin1String(":memory:") || file.startsWith(QLatin1String("file:"));
    if (readOnly && !special && !QFileInfo::exists(file))
        return fail(Error {
            ErrorKind::NotFound, Text::tr("The database file does not exist."), file, {}, {} });

    int flags = SQLITE_OPEN_URI | SQLITE_OPEN_NOMUTEX;
    flags |= readOnly ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    sqlite3 *db = nullptr;
    const int rc = sqlite3_open_v2(file.toUtf8().constData(), &db, flags, nullptr);
    if (rc != SQLITE_OK) {
        Error error = makeError(db, rc);
        sqlite3_close(db);
        return fail(std::move(error));
    }
    sqlite3_busy_timeout(db, busyMs < 0 ? 0 : busyMs);
    char *message = nullptr;
    if (sqlite3_exec(db, "PRAGMA foreign_keys=ON", nullptr, nullptr, &message) != SQLITE_OK) {
        Error error = makeError(db, SQLITE_ERROR);
        sqlite3_free(message);
        sqlite3_close(db);
        return fail(std::move(error));
    }
    {
        std::lock_guard lock(m_state->mutex);
        m_state->db = db;
    }
    return ServerInfo { QStringLiteral("SQLite"), QString::fromLatin1(sqlite3_libversion()) };
}

void SqliteConnection::close()
{
    closeDb();
}

void SqliteConnection::closeDb()
{
    std::lock_guard lock(m_state->mutex);
    if (m_state->db) {
        // v2: open streams still hold statements; the handle is freed when they finalize.
        sqlite3_close_v2(m_state->db);
        m_state->db = nullptr;
    }
}

Result<void> SqliteConnection::exec(const char *sql)
{
    CallScope scope(*m_state);
    if (!m_state->db)
        return fail(notOpenError());
    const int rc = sqlite3_exec(m_state->db, sql, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK)
        return fail(makeError(m_state->db, rc));
    return {};
}

Result<std::unique_ptr<IResultStream>> SqliteConnection::execute(const Statement &statement,
                                                                 const CancellationToken &token)
{
    CallScope scope(*m_state);
    sqlite3 *db = m_state->db;
    if (!db)
        return fail(notOpenError());
    if (token.isCancelled())
        return fail(cancelledError());

    const QByteArray sql = statement.sql.toUtf8();
    sqlite3_stmt *stmt = nullptr;
    const char *tail = nullptr;
    int rc = sqlite3_prepare_v3(db, sql.constData(), static_cast<int>(sql.size()) + 1, 0, &stmt,
                                &tail);
    if (rc != SQLITE_OK)
        return fail(makeError(db, rc, sql, true));
    if (tail && !tailIsBlank(tail)) {
        sqlite3_finalize(stmt);
        return fail(
            Error { ErrorKind::Unsupported, Text::tr("Run one statement at a time."), {}, {}, {} });
    }

    std::vector<ColumnInfo> columns;
    if (stmt) {
        const int count = sqlite3_column_count(stmt);
        columns.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            ColumnInfo info;
            info.name = QString::fromUtf8(sqlite3_column_name(stmt, i));
            if (const char *decl = sqlite3_column_decltype(stmt, i)) {
                info.nativeType = QString::fromUtf8(decl);
                info.type = declaredValueType(QByteArray(decl));
            }
            const char *table = sqlite3_column_table_name(stmt, i);
            const char *database = sqlite3_column_database_name(stmt, i);
            if (table && database)
                info.sourceTable
                    = ObjectPath { { QString::fromUtf8(database), QString::fromUtf8(table) },
                                   ObjectKind::Table };
            columns.push_back(std::move(info));
        }
    }
    // A null stmt is empty or comment-only text: the stream below is empty and already at its end.
    if (stmt) {
        const int expected = sqlite3_bind_parameter_count(stmt);
        if (expected != statement.parameters.size()) {
            sqlite3_finalize(stmt);
            return fail(Error { ErrorKind::Syntax,
                                Text::tr("The statement expects %1 parameter(s) but %2 were given.")
                                    .arg(expected)
                                    .arg(statement.parameters.size()),
                                {},
                                {},
                                {} });
        }
        for (int i = 0; i < expected; ++i) {
            rc = bindValue(stmt, i + 1, statement.parameters.at(i));
            if (rc != SQLITE_OK) {
                Error error = makeError(db, rc);
                sqlite3_finalize(stmt);
                return fail(std::move(error));
            }
        }
    }

    if (m_state->cancel.load(std::memory_order_acquire)) {
        sqlite3_finalize(stmt);
        return fail(cancelledError());
    }

    auto stream = std::make_unique<SqliteStream>(m_state, stmt, std::move(columns));
    if (stream->columns().empty()) { // INSERT/UPDATE/DDL: run it now, it has no rows to stream
        auto done = stream->run(1, token);
        if (!done)
            return fail(std::move(done).error());
    }
    return stream;
}

} // namespace quarry::sqlite
