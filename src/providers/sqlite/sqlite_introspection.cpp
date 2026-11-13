#include "sqlite_connection.h"
#include "sqlite_dialect.h"
#include "sqlite_errors.h"
#include "sqlite_stream.h"
#include "sqlite_text.h"

#include <algorithm>
#include <map>

namespace quarry::sqlite {

namespace {

const QString kTables = QStringLiteral("Tables");
const QString kViews = QStringLiteral("Views");
const QString kColumns = QStringLiteral("Columns");
const QString kIndexes = QStringLiteral("Indexes");
const QString kForeignKeys = QStringLiteral("Foreign keys");

QString quoted(const QString &identifier)
{
    return SqliteDialect().quoteIdentifier(identifier);
}

SchemaNode node(QStringList parts, ObjectKind kind, const QString &name, bool hasChildren,
                const QString &detail = {})
{
    SchemaNode out;
    out.path = ObjectPath { std::move(parts), kind };
    out.name = name;
    out.hasChildren = hasChildren;
    out.detail = detail;
    return out;
}

SchemaNode folder(const QStringList &parent, const QString &key, const QString &label)
{
    return node(parent + QStringList { key }, ObjectKind::Folder, label, true);
}

QString fkName(const QStringList &from, const QString &table, const QStringList &to)
{
    QString name = from.join(QLatin1String(", ")) + QStringLiteral(" -> ") + table;
    if (!to.isEmpty())
        name += QStringLiteral("(") + to.join(QLatin1String(", ")) + QStringLiteral(")");
    return name;
}

struct FkRow
{
    QStringList from;
    QString table;
    QStringList to;
};

} // namespace

Result<SqliteConnection::Rows> SqliteConnection::queryRows(const QString &sql,
                                                           const QStringList &args)
{
    sqlite3 *db = m_state->db;
    const QByteArray utf8 = sql.toUtf8();
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, utf8.constData(), static_cast<int>(utf8.size()) + 1, &stmt,
                                nullptr);
    if (rc != SQLITE_OK)
        return fail(makeError(db, rc, utf8, true));
    for (qsizetype i = 0; i < args.size(); ++i) {
        const QByteArray arg = args.at(i).toUtf8();
        sqlite3_bind_text(stmt, static_cast<int>(i) + 1, arg.constData(),
                          static_cast<int>(arg.size()), SQLITE_TRANSIENT);
    }
    Rows rows;
    const int columns = sqlite3_column_count(stmt);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        QVariantList row;
        row.reserve(columns);
        for (int c = 0; c < columns; ++c) {
            switch (sqlite3_column_type(stmt, c)) {
            case SQLITE_INTEGER:
                row.push_back(static_cast<qlonglong>(sqlite3_column_int64(stmt, c)));
                break;
            case SQLITE_FLOAT:
                row.push_back(sqlite3_column_double(stmt, c));
                break;
            case SQLITE_NULL:
                row.push_back(QVariant());
                break;
            default:
                row.push_back(
                    QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, c)),
                                      sqlite3_column_bytes(stmt, c)));
                break;
            }
        }
        rows.push_back(std::move(row));
    }
    if (rc != SQLITE_DONE) {
        Error error = rc == SQLITE_INTERRUPT ? cancelledError() : makeError(db, rc);
        sqlite3_finalize(stmt);
        return fail(std::move(error));
    }
    sqlite3_finalize(stmt);
    return rows;
}

Result<std::vector<SchemaNode>> SqliteConnection::rootNodes()
{
    auto databases = queryRows(QStringLiteral("PRAGMA database_list"));
    if (!databases)
        return fail(std::move(databases).error());
    std::vector<SchemaNode> out;
    for (const QVariantList &row : databases.value()) {
        const QString name = row.at(1).toString();
        if (name == QLatin1String("temp")) { // only worth showing once it holds something
            auto count = queryRows(QStringLiteral("SELECT count(*) FROM temp.sqlite_schema"));
            if (!count)
                return fail(std::move(count).error());
            if (count.value().front().front().toLongLong() == 0)
                continue;
        }
        out.push_back(node({ name }, ObjectKind::Database, name, true, row.at(2).toString()));
    }
    return out;
}

Result<std::vector<SchemaNode>> SqliteConnection::folderItems(const QStringList &parts)
{
    const QString &db = parts.at(0);
    const QString &key = parts.at(1);
    const bool tables = key == kTables;
    if (!tables && key != kViews)
        return std::vector<SchemaNode> {};
    auto rows = queryRows(QStringLiteral("SELECT name FROM %1.sqlite_schema WHERE type = ?1 "
                                         "AND name NOT LIKE 'sqlite\\_%' ESCAPE '\\' ORDER BY name")
                              .arg(quoted(db)),
                          { tables ? QStringLiteral("table") : QStringLiteral("view") });
    if (!rows)
        return fail(std::move(rows).error());
    std::vector<SchemaNode> out;
    for (const QVariantList &row : rows.value()) {
        const QString name = row.front().toString();
        out.push_back(
            node({ db, name }, tables ? ObjectKind::Table : ObjectKind::View, name, true));
    }
    return out;
}

Result<std::vector<SchemaNode>> SqliteConnection::tableItems(const QStringList &parts)
{
    const QString &db = parts.at(0);
    const QString &table = parts.at(1);
    const QString &key = parts.at(2);
    const QStringList args { table, db };
    std::vector<SchemaNode> out;

    if (key == kColumns) {
        auto rows = queryRows(QStringLiteral("SELECT name, type, \"notnull\", pk FROM "
                                             "pragma_table_info(?1, ?2) ORDER BY cid"),
                              args);
        if (!rows)
            return fail(std::move(rows).error());
        for (const QVariantList &row : rows.value()) {
            QStringList detail;
            if (const QString type = row.at(1).toString(); !type.isEmpty())
                detail << type;
            if (row.at(2).toLongLong() != 0)
                detail << QStringLiteral("NOT NULL");
            if (row.at(3).toLongLong() != 0)
                detail << QStringLiteral("PK");
            const QString name = row.at(0).toString();
            out.push_back(node(parts.mid(0, 2) + QStringList { name }, ObjectKind::Column, name,
                               false, detail.join(QLatin1Char(' '))));
        }
    } else if (key == kIndexes) {
        auto rows = queryRows(QStringLiteral("SELECT name, \"unique\" FROM pragma_index_list(?1, "
                                             "?2) WHERE origin <> 'pk' ORDER BY seq"),
                              args);
        if (!rows)
            return fail(std::move(rows).error());
        for (const QVariantList &row : rows.value()) {
            const QString name = row.at(0).toString();
            out.push_back(node(parts.mid(0, 2) + QStringList { name }, ObjectKind::Index, name,
                               false,
                               row.at(1).toLongLong() != 0 ? QStringLiteral("UNIQUE") : QString()));
        }
    } else if (key == kForeignKeys) {
        auto fks = describeTable(ObjectPath { parts.mid(0, 2), ObjectKind::Table });
        if (!fks)
            return fail(std::move(fks).error());
        for (const ForeignKeyDef &fk : fks.value().foreignKeys) {
            const QString target = fk.referencedTable.parts.value(1);
            out.push_back(node(parts.mid(0, 2) + QStringList { fk.name }, ObjectKind::ForeignKey,
                               fk.name, false,
                               QStringLiteral("-> ") + target
                                   + (fk.referencedColumns.isEmpty()
                                          ? QString()
                                          : QStringLiteral("(%1)").arg(
                                                fk.referencedColumns.join(QLatin1String(", "))))));
        }
    }
    return out;
}

Result<std::vector<SchemaNode>> SqliteConnection::children(const ObjectPath &parent,
                                                           const CancellationToken &token)
{
    CallScope scope(*m_state);
    if (!m_state->db)
        return fail(
            Error { ErrorKind::Connection, Text::tr("The connection is not open."), {}, {}, {} });
    if (token.isCancelled())
        return fail(cancelledError());

    const QStringList &parts = parent.parts;
    if (parts.isEmpty())
        return rootNodes();
    switch (parent.kind) {
    case ObjectKind::Database:
        if (parts.size() == 1)
            return std::vector<SchemaNode> { folder(parts, kTables, Text::tr("Tables")),
                                             folder(parts, kViews, Text::tr("Views")) };
        break;
    case ObjectKind::Folder:
        if (parts.size() == 2)
            return folderItems(parts);
        if (parts.size() == 3)
            return tableItems(parts);
        break;
    case ObjectKind::Table:
    case ObjectKind::View:
        if (parts.size() == 2) {
            std::vector<SchemaNode> out { folder(parts, kColumns, Text::tr("Columns")) };
            if (parent.kind == ObjectKind::Table) {
                out.push_back(folder(parts, kIndexes, Text::tr("Indexes")));
                out.push_back(folder(parts, kForeignKeys, Text::tr("Foreign keys")));
            }
            return out;
        }
        break;
    default:
        break;
    }
    return std::vector<SchemaNode> {};
}

Result<TableDetails> SqliteConnection::describe(const ObjectPath &table,
                                                const CancellationToken &token)
{
    CallScope scope(*m_state);
    if (!m_state->db)
        return fail(
            Error { ErrorKind::Connection, Text::tr("The connection is not open."), {}, {}, {} });
    if (token.isCancelled())
        return fail(cancelledError());
    return describeTable(table);
}

Result<TableDetails> SqliteConnection::describeTable(const ObjectPath &table)
{
    if (table.parts.size() != 2)
        return fail(Error { ErrorKind::NotFound,
                            Text::tr("The path does not point to a table or view."),
                            {},
                            {},
                            {} });

    const QString &db = table.parts.at(0);
    const QString &name = table.parts.at(1);
    const QStringList args { name, db };
    TableDetails details;

    auto columns = queryRows(QStringLiteral("SELECT name, type, \"notnull\", dflt_value, pk FROM "
                                            "pragma_table_info(?1, ?2) ORDER BY cid"),
                             args);
    if (!columns)
        return fail(std::move(columns).error());
    if (columns.value().empty())
        return fail(Error { ErrorKind::NotFound,
                            Text::tr("The table \"%1\" does not exist.").arg(name),
                            {},
                            {},
                            {} });
    std::map<qlonglong, QString> pk; // position in the key -> column
    for (const QVariantList &row : columns.value()) {
        ColumnDef def;
        def.name = row.at(0).toString();
        def.nativeType = row.at(1).toString();
        def.type = declaredValueType(def.nativeType.toUtf8());
        def.nullable = row.at(2).toLongLong() == 0;
        def.defaultExpression = row.at(3).toString();
        if (const qlonglong position = row.at(4).toLongLong(); position > 0)
            pk[position] = def.name;
        details.columns.push_back(std::move(def));
    }
    if (!pk.empty()) {
        QStringList key;
        for (const auto &entry : pk)
            key << entry.second;
        details.primaryKey = key;
    }

    auto indexes = queryRows(QStringLiteral("SELECT name, \"unique\" FROM pragma_index_list(?1, "
                                            "?2) WHERE origin <> 'pk' ORDER BY seq"),
                             args);
    if (!indexes)
        return fail(std::move(indexes).error());
    for (const QVariantList &row : indexes.value()) {
        IndexDef def;
        def.name = row.at(0).toString();
        def.unique = row.at(1).toLongLong() != 0;
        auto cols = queryRows(QStringLiteral("SELECT name FROM pragma_index_info(?1, ?2) "
                                             "ORDER BY seqno"),
                              { def.name, db });
        if (!cols)
            return fail(std::move(cols).error());
        for (const QVariantList &c : cols.value())
            def.columns << c.front().toString(); // expression columns come back as NULL -> ""
        details.indexes.push_back(std::move(def));
    }

    auto fks = queryRows(QStringLiteral("SELECT id, \"table\", \"from\", \"to\" FROM "
                                        "pragma_foreign_key_list(?1, ?2) ORDER BY id, seq"),
                         args);
    if (!fks)
        return fail(std::move(fks).error());
    std::map<qlonglong, FkRow> grouped;
    for (const QVariantList &row : fks.value()) {
        FkRow &fk = grouped[row.at(0).toLongLong()];
        fk.table = row.at(1).toString();
        fk.from << row.at(2).toString();
        if (!row.at(3).isNull())
            fk.to << row.at(3).toString();
    }
    for (const auto &entry : grouped) {
        const FkRow &fk = entry.second;
        details.foreignKeys.push_back({ fkName(fk.from, fk.table, fk.to), fk.from,
                                        ObjectPath { { db, fk.table }, ObjectKind::Table },
                                        fk.to });
    }
    return details;
}

} // namespace quarry::sqlite
