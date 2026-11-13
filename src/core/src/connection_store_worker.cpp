#include "connection_store_worker.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <memory>
#include <sqlite3.h>

namespace quarry {

namespace {

constexpr int kLatestVersion = 1;
constexpr int kBusyTimeoutMs = 3000;

// Migration N (1-based) is kMigrations[N - 1]. Never edit a released one: add a new entry.
// `values` is a reserved word, hence values_json.
constexpr const char *kMigrations[kLatestVersion] = {
    "CREATE TABLE connections("
    " id TEXT PRIMARY KEY NOT NULL,"
    " name TEXT NOT NULL,"
    " provider_id TEXT NOT NULL,"
    " values_json TEXT NOT NULL DEFAULT '{}',"
    " color_index INTEGER NOT NULL DEFAULT -1,"
    " production INTEGER NOT NULL DEFAULT 0,"
    " password_mode INTEGER NOT NULL DEFAULT 0,"
    " created_at TEXT NOT NULL,"
    " updated_at TEXT NOT NULL,"
    " last_used_at TEXT)",
};

constexpr const char *kColumns = "id, name, provider_id, values_json, color_index, production,"
                                 " password_mode, created_at, updated_at, last_used_at";

using Stmt = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

QString columnText(sqlite3_stmt *stmt, int column)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, column)),
                             sqlite3_column_bytes(stmt, column));
}

void bindText(sqlite3_stmt *stmt, int index, const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    sqlite3_bind_text(stmt, index, utf8.constData(), static_cast<int>(utf8.size()),
                      SQLITE_TRANSIENT);
}

QString toIso(const QDateTime &time)
{
    return time.toUTC().toString(Qt::ISODateWithMs);
}

QDateTime fromIso(const QString &text)
{
    return QDateTime::fromString(text, Qt::ISODateWithMs).toUTC();
}

ConnectionProfile readRow(sqlite3_stmt *stmt)
{
    ConnectionProfile p;
    p.id = QUuid::fromString(columnText(stmt, 0));
    p.name = columnText(stmt, 1);
    p.providerId = columnText(stmt, 2);
    p.values = QJsonDocument::fromJson(columnText(stmt, 3).toUtf8()).object().toVariantMap();
    p.colorIndex = sqlite3_column_int(stmt, 4);
    p.production = sqlite3_column_int(stmt, 5) != 0;
    const int mode = sqlite3_column_int(stmt, 6);
    p.passwordMode = mode >= static_cast<int>(PasswordMode::None)
            && mode <= static_cast<int>(PasswordMode::AskEveryTime)
        ? static_cast<PasswordMode>(mode)
        : PasswordMode::None;
    p.createdAt = fromIso(columnText(stmt, 7));
    p.updatedAt = fromIso(columnText(stmt, 8));
    if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
        p.lastUsedAt = fromIso(columnText(stmt, 9));
    return p;
}

} // namespace

ConnectionStoreWorker::ConnectionStoreWorker(QString databasePath)
    : m_path(std::move(databasePath))
{ }

ConnectionStoreWorker::~ConnectionStoreWorker()
{
    closeDatabase();
}

void ConnectionStoreWorker::closeDatabase()
{
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

Error ConnectionStoreWorker::sqliteError(int rc, const QString &context) const
{
    const QString detail = QStringLiteral("%1 (SQLite %2: %3) [%4]")
                               .arg(context)
                               .arg(rc)
                               .arg(m_db ? QString::fromUtf8(sqlite3_errmsg(m_db))
                                         : QString::fromUtf8(sqlite3_errstr(rc)),
                                    m_path);
    switch (rc & 0xff) {
    case SQLITE_BUSY:
    case SQLITE_LOCKED:
        return { ErrorKind::Connection,
                 tr("Quarry's database is in use by another process. Try again in a moment."),
                 detail,
                 {},
                 {} };
    case SQLITE_NOTADB:
    case SQLITE_CORRUPT:
        return { ErrorKind::Driver,
                 tr("Quarry's database file is damaged or is not a database."),
                 detail,
                 {},
                 {} };
    case SQLITE_CANTOPEN:
    case SQLITE_PERM:
    case SQLITE_READONLY:
    case SQLITE_AUTH:
        return { ErrorKind::Permission,
                 tr("Quarry cannot read or write its database file."),
                 detail,
                 {},
                 {} };
    default:
        return { ErrorKind::Internal, tr("Quarry could not access its database."), detail, {}, {} };
    }
}

bool ConnectionStoreWorker::exec(const char *sql, Error &error)
{
    char *message = nullptr;
    const int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        error = sqliteError(rc, QString::fromUtf8(sql));
        sqlite3_free(message);
        return false;
    }
    return true;
}

bool ConnectionStoreWorker::migrate(Error &error)
{
    int version = 0;
    {
        sqlite3_stmt *raw = nullptr;
        int rc = sqlite3_prepare_v2(m_db, "PRAGMA user_version", -1, &raw, nullptr);
        Stmt stmt(raw, &sqlite3_finalize);
        if (rc == SQLITE_OK)
            rc = sqlite3_step(stmt.get());
        if (rc != SQLITE_ROW) {
            error = sqliteError(rc, QStringLiteral("PRAGMA user_version"));
            return false;
        }
        version = sqlite3_column_int(stmt.get(), 0);
    }
    if (version > kLatestVersion) {
        error = { ErrorKind::Unsupported,
                  tr("This database was created by a newer version of Quarry."),
                  tr("Database version %1; this version of Quarry understands up to %2. [%3]")
                      .arg(version)
                      .arg(kLatestVersion)
                      .arg(m_path),
                  {},
                  {} };
        return false;
    }

    // Header-touching pragma only after the version check, so a newer database stays untouched.
    if (!exec("PRAGMA journal_mode=WAL", error))
        return false;
    if (version == kLatestVersion)
        return true;

    if (!exec("BEGIN IMMEDIATE", error))
        return false;
    for (int v = version; v < kLatestVersion; ++v) {
        if (!exec(kMigrations[v], error)) {
            sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
            return false;
        }
    }
    const QByteArray setVersion = "PRAGMA user_version=" + QByteArray::number(kLatestVersion);
    if (!exec(setVersion.constData(), error) || !exec("COMMIT", error)) {
        sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
    return true;
}

bool ConnectionStoreWorker::ensureOpen(Error &error)
{
    if (m_db)
        return true;

    if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
        error = { ErrorKind::Permission,
                  tr("Quarry cannot read or write its database file."),
                  tr("Could not create the folder of %1.").arg(m_path),
                  {},
                  {} };
        return false;
    }
    const int rc = sqlite3_open_v2(m_path.toUtf8().constData(), &m_db,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
                                   nullptr);
    if (rc != SQLITE_OK) {
        error = sqliteError(rc, QStringLiteral("open"));
        closeDatabase();
        return false;
    }
    sqlite3_busy_timeout(m_db, kBusyTimeoutMs);
    if (!exec("PRAGMA foreign_keys=ON", error) || !migrate(error)) {
        closeDatabase();
        return false;
    }
    return true;
}

bool ConnectionStoreWorker::selectOne(const QUuid &id, ConnectionProfile &out, Error &error)
{
    const QByteArray sql = QByteArray("SELECT ") + kColumns + " FROM connections WHERE id=?1";
    sqlite3_stmt *raw = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.constData(), -1, &raw, nullptr);
    Stmt stmt(raw, &sqlite3_finalize);
    if (rc == SQLITE_OK) {
        bindText(stmt.get(), 1, id.toString(QUuid::WithoutBraces));
        rc = sqlite3_step(stmt.get());
    }
    if (rc == SQLITE_ROW) {
        out = readRow(stmt.get());
        return true;
    }
    error = rc == SQLITE_DONE ? Error { ErrorKind::NotFound,
                                        tr("The connection no longer exists."),
                                        id.toString(QUuid::WithoutBraces),
                                        {},
                                        {} }
                              : sqliteError(rc, QStringLiteral("select"));
    return false;
}

void ConnectionStoreWorker::load()
{
    Error error;
    if (!ensureOpen(error)) {
        emit failed(error);
        return;
    }
    const QByteArray sql = QByteArray("SELECT ") + kColumns
        + " FROM connections ORDER BY name COLLATE NOCASE, created_at";
    sqlite3_stmt *raw = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.constData(), -1, &raw, nullptr);
    Stmt stmt(raw, &sqlite3_finalize);
    QList<ConnectionProfile> list;
    if (rc == SQLITE_OK) {
        while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW)
            list.append(readRow(stmt.get()));
    }
    if (rc != SQLITE_DONE) {
        emit failed(sqliteError(rc, QStringLiteral("load")));
        return;
    }
    emit loaded(list);
}

void ConnectionStoreWorker::save(const ConnectionProfile &input, const QStringList &secretKeys)
{
    if (input.name.trimmed().isEmpty()) {
        emit failed({ ErrorKind::Constraint, tr("Enter a name for the connection."), {}, {}, {} });
        return;
    }
    if (input.providerId.trimmed().isEmpty()) {
        emit failed({ ErrorKind::Constraint, tr("Choose a database engine."), {}, {}, {} });
        return;
    }
    Error error;
    if (!ensureOpen(error)) {
        emit failed(error);
        return;
    }

    ConnectionProfile p = input;
    if (p.id.isNull())
        p.id = QUuid::createUuid();
    for (auto it = p.values.begin(); it != p.values.end();) {
        if (secretKeys.contains(it.key())
            || it.key().compare(QLatin1String("password"), Qt::CaseInsensitive) == 0)
            it = p.values.erase(it);
        else
            ++it;
    }

    const QString now = toIso(QDateTime::currentDateTimeUtc());
    const QString json = QString::fromUtf8(
        QJsonDocument(QJsonObject::fromVariantMap(p.values)).toJson(QJsonDocument::Compact));
    constexpr const char *kUpsert
        = "INSERT INTO connections(id, name, provider_id, values_json, color_index, production,"
          " password_mode, created_at, updated_at) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?8)"
          " ON CONFLICT(id) DO UPDATE SET name=excluded.name, provider_id=excluded.provider_id,"
          " values_json=excluded.values_json, color_index=excluded.color_index,"
          " production=excluded.production, password_mode=excluded.password_mode,"
          " updated_at=excluded.updated_at";
    sqlite3_stmt *raw = nullptr;
    int rc = sqlite3_prepare_v2(m_db, kUpsert, -1, &raw, nullptr);
    Stmt stmt(raw, &sqlite3_finalize);
    if (rc == SQLITE_OK) {
        bindText(stmt.get(), 1, p.id.toString(QUuid::WithoutBraces));
        bindText(stmt.get(), 2, p.name);
        bindText(stmt.get(), 3, p.providerId);
        bindText(stmt.get(), 4, json);
        sqlite3_bind_int(stmt.get(), 5, p.colorIndex);
        sqlite3_bind_int(stmt.get(), 6, p.production ? 1 : 0);
        sqlite3_bind_int(stmt.get(), 7, static_cast<int>(p.passwordMode));
        bindText(stmt.get(), 8, now);
        rc = sqlite3_step(stmt.get());
    }
    if (rc != SQLITE_DONE) {
        emit failed(sqliteError(rc, QStringLiteral("save")));
        return;
    }
    stmt.reset();

    ConnectionProfile stored;
    if (!selectOne(p.id, stored, error)) {
        emit failed(error);
        return;
    }
    emit saved(stored);
}

void ConnectionStoreWorker::remove(const QUuid &id)
{
    Error error;
    if (!ensureOpen(error)) {
        emit failed(error);
        return;
    }
    sqlite3_stmt *raw = nullptr;
    int rc = sqlite3_prepare_v2(m_db, "DELETE FROM connections WHERE id=?1", -1, &raw, nullptr);
    Stmt stmt(raw, &sqlite3_finalize);
    if (rc == SQLITE_OK) {
        bindText(stmt.get(), 1, id.toString(QUuid::WithoutBraces));
        rc = sqlite3_step(stmt.get());
    }
    if (rc != SQLITE_DONE) {
        emit failed(sqliteError(rc, QStringLiteral("remove")));
        return;
    }
    emit removed(id);
}

void ConnectionStoreWorker::touch(const QUuid &id)
{
    Error error;
    if (!ensureOpen(error)) {
        emit failed(error);
        return;
    }
    sqlite3_stmt *raw = nullptr;
    int rc = sqlite3_prepare_v2(m_db, "UPDATE connections SET last_used_at=?2 WHERE id=?1", -1,
                                &raw, nullptr);
    Stmt stmt(raw, &sqlite3_finalize);
    if (rc == SQLITE_OK) {
        bindText(stmt.get(), 1, id.toString(QUuid::WithoutBraces));
        bindText(stmt.get(), 2, toIso(QDateTime::currentDateTimeUtc()));
        rc = sqlite3_step(stmt.get());
    }
    if (rc != SQLITE_DONE) {
        emit failed(sqliteError(rc, QStringLiteral("touch")));
        return;
    }
    ConnectionProfile stored;
    if (!selectOne(id, stored, error)) { // also covers "no such row"
        emit failed(error);
        return;
    }
    emit saved(stored);
}

} // namespace quarry
