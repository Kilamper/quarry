#include "connection_store_worker.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <memory>
#include <sqlite3.h>

namespace quarry {

namespace {

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
    : m_database(std::move(databasePath))
{ }

void ConnectionStoreWorker::closeDatabase()
{
    m_database.close();
}

bool ConnectionStoreWorker::selectOne(const QUuid &id, ConnectionProfile &out, Error &error)
{
    const QByteArray sql = QByteArray("SELECT ") + kColumns + " FROM connections WHERE id=?1";
    sqlite3_stmt *raw = nullptr;
    int rc = sqlite3_prepare_v2(m_database.handle(), sql.constData(), -1, &raw, nullptr);
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
                              : m_database.error(rc, QStringLiteral("select"));
    return false;
}

void ConnectionStoreWorker::load()
{
    Error error;
    if (!m_database.open(error)) {
        emit failed(error);
        return;
    }
    const QByteArray sql = QByteArray("SELECT ") + kColumns
        + " FROM connections ORDER BY name COLLATE NOCASE, created_at";
    sqlite3_stmt *raw = nullptr;
    int rc = sqlite3_prepare_v2(m_database.handle(), sql.constData(), -1, &raw, nullptr);
    Stmt stmt(raw, &sqlite3_finalize);
    QList<ConnectionProfile> list;
    if (rc == SQLITE_OK) {
        while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW)
            list.append(readRow(stmt.get()));
    }
    if (rc != SQLITE_DONE) {
        emit failed(m_database.error(rc, QStringLiteral("load")));
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
    if (!m_database.open(error)) {
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
    int rc = sqlite3_prepare_v2(m_database.handle(), kUpsert, -1, &raw, nullptr);
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
        emit failed(m_database.error(rc, QStringLiteral("save")));
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
    if (!m_database.open(error)) {
        emit failed(error);
        return;
    }
    sqlite3_stmt *raw = nullptr;
    int rc = sqlite3_prepare_v2(m_database.handle(), "DELETE FROM connections WHERE id=?1", -1,
                                &raw, nullptr);
    Stmt stmt(raw, &sqlite3_finalize);
    if (rc == SQLITE_OK) {
        bindText(stmt.get(), 1, id.toString(QUuid::WithoutBraces));
        rc = sqlite3_step(stmt.get());
    }
    if (rc != SQLITE_DONE) {
        emit failed(m_database.error(rc, QStringLiteral("remove")));
        return;
    }
    emit removed(id);
}

void ConnectionStoreWorker::touch(const QUuid &id)
{
    Error error;
    if (!m_database.open(error)) {
        emit failed(error);
        return;
    }
    sqlite3_stmt *raw = nullptr;
    int rc = sqlite3_prepare_v2(m_database.handle(),
                                "UPDATE connections SET last_used_at=?2 WHERE id=?1", -1, &raw,
                                nullptr);
    Stmt stmt(raw, &sqlite3_finalize);
    if (rc == SQLITE_OK) {
        bindText(stmt.get(), 1, id.toString(QUuid::WithoutBraces));
        bindText(stmt.get(), 2, toIso(QDateTime::currentDateTimeUtc()));
        rc = sqlite3_step(stmt.get());
    }
    if (rc != SQLITE_DONE) {
        emit failed(m_database.error(rc, QStringLiteral("touch")));
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
