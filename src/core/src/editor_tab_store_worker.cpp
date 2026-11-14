#include "editor_tab_store_worker.h"

#include <memory>
#include <sqlite3.h>

namespace quarry {

namespace {

constexpr const char *kColumns
    = "id, connection_id, title, text, cursor_position, position, updated_at";

using Stmt = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

QString columnText(sqlite3_stmt *stmt, int column)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, column)),
                             sqlite3_column_bytes(stmt, column));
}

// Same text format as connections.id, or the foreign key would never match.
QString idText(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

void bindText(sqlite3_stmt *stmt, int index, const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    sqlite3_bind_text(stmt, index, utf8.constData(), static_cast<int>(utf8.size()),
                      SQLITE_TRANSIENT);
}

EditorTab readRow(sqlite3_stmt *stmt)
{
    EditorTab t;
    t.id = QUuid::fromString(columnText(stmt, 0));
    if (sqlite3_column_type(stmt, 1) != SQLITE_NULL)
        t.connectionId = QUuid::fromString(columnText(stmt, 1));
    t.title = columnText(stmt, 2);
    t.text = columnText(stmt, 3);
    t.cursorPosition = sqlite3_column_int(stmt, 4);
    t.position = sqlite3_column_int(stmt, 5);
    t.updatedAt = QDateTime::fromString(columnText(stmt, 6), Qt::ISODateWithMs).toUTC();
    return t;
}

} // namespace

EditorTabStoreWorker::EditorTabStoreWorker(QString databasePath)
    : m_database(std::move(databasePath))
{ }

void EditorTabStoreWorker::closeDatabase()
{
    m_database.close();
}

void EditorTabStoreWorker::load()
{
    Error error;
    if (!m_database.open(error)) {
        emit failed(error);
        return;
    }
    const QByteArray sql
        = QByteArray("SELECT ") + kColumns + " FROM editor_tabs ORDER BY position, updated_at, id";
    sqlite3_stmt *raw = nullptr;
    int rc = sqlite3_prepare_v2(m_database.handle(), sql.constData(), -1, &raw, nullptr);
    Stmt stmt(raw, &sqlite3_finalize);
    QList<EditorTab> list;
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

void EditorTabStoreWorker::save(const EditorTab &input)
{
    if (input.title.trimmed().isEmpty()) {
        emit failed({ ErrorKind::Constraint, tr("Enter a title for the tab."), {}, {}, {} });
        return;
    }
    Error error;
    if (!m_database.open(error)) {
        emit failed(error);
        return;
    }

    EditorTab t = input;
    if (t.id.isNull())
        t.id = QUuid::createUuid();
    t.updatedAt = QDateTime::currentDateTimeUtc();

    constexpr const char *kUpsert
        = "INSERT INTO editor_tabs(id, connection_id, title, text, cursor_position, position,"
          " updated_at) VALUES(?1,?2,?3,?4,?5,?6,?7)"
          " ON CONFLICT(id) DO UPDATE SET connection_id=excluded.connection_id,"
          " title=excluded.title, text=excluded.text, cursor_position=excluded.cursor_position,"
          " position=excluded.position, updated_at=excluded.updated_at";
    sqlite3_stmt *raw = nullptr;
    int rc = sqlite3_prepare_v2(m_database.handle(), kUpsert, -1, &raw, nullptr);
    Stmt stmt(raw, &sqlite3_finalize);
    if (rc == SQLITE_OK) {
        bindText(stmt.get(), 1, idText(t.id));
        if (t.connectionId)
            bindText(stmt.get(), 2, idText(*t.connectionId));
        else
            sqlite3_bind_null(stmt.get(), 2);
        bindText(stmt.get(), 3, t.title);
        bindText(stmt.get(), 4, t.text);
        sqlite3_bind_int(stmt.get(), 5, t.cursorPosition);
        sqlite3_bind_int(stmt.get(), 6, t.position);
        bindText(stmt.get(), 7, t.updatedAt.toString(Qt::ISODateWithMs));
        rc = sqlite3_step(stmt.get());
    }
    if (rc != SQLITE_DONE) {
        // Only the foreign key can fail a constraint here: id conflicts are upserts.
        if ((rc & 0xff) == SQLITE_CONSTRAINT && t.connectionId) {
            emit failed({ ErrorKind::Constraint,
                          tr("The connection of this tab no longer exists."),
                          m_database.error(rc, QStringLiteral("save")).detail,
                          {},
                          {} });
            return;
        }
        emit failed(m_database.error(rc, QStringLiteral("save")));
        return;
    }
    emit saved(t);
}

void EditorTabStoreWorker::remove(const QUuid &id)
{
    Error error;
    if (!m_database.open(error)) {
        emit failed(error);
        return;
    }
    sqlite3_stmt *raw = nullptr;
    int rc = sqlite3_prepare_v2(m_database.handle(), "DELETE FROM editor_tabs WHERE id=?1", -1,
                                &raw, nullptr);
    Stmt stmt(raw, &sqlite3_finalize);
    if (rc == SQLITE_OK) {
        bindText(stmt.get(), 1, idText(id));
        rc = sqlite3_step(stmt.get());
    }
    if (rc != SQLITE_DONE) {
        emit failed(m_database.error(rc, QStringLiteral("remove")));
        return;
    }
    emit removed(id);
}

} // namespace quarry
