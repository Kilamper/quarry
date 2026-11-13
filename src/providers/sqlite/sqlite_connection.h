#pragma once

#include "sqlite_db_state.h"

#include <quarry/provider_api.h>

#include <memory>
#include <vector>

namespace quarry::sqlite {

class SqliteConnection : public IConnection
{
public:
    explicit SqliteConnection(ConnectionConfig config);
    ~SqliteConnection() override;
    SqliteConnection(const SqliteConnection &) = delete;
    SqliteConnection &operator=(const SqliteConnection &) = delete;

    Result<ServerInfo> open(const CancellationToken &) override;
    void close() override;
    bool isOpen() const override { return m_state->db != nullptr; }

    Result<std::vector<SchemaNode>> children(const ObjectPath &parent,
                                             const CancellationToken &) override;
    Result<TableDetails> describe(const ObjectPath &table, const CancellationToken &) override;
    Result<std::unique_ptr<IResultStream>> execute(const Statement &,
                                                   const CancellationToken &) override;
    void requestCancel() override { m_state->requestCancel(); }

    Result<void> begin() override { return exec("BEGIN"); }
    Result<void> commit() override { return exec("COMMIT"); }
    Result<void> rollback() override { return exec("ROLLBACK"); }

private:
    using Rows = std::vector<QVariantList>;

    void closeDb();
    Result<void> exec(const char *sql);
    // Runs a read-only query with text parameters and collects every row. No CallScope.
    Result<Rows> queryRows(const QString &sql, const QStringList &args = {});
    Result<TableDetails> describeTable(const ObjectPath &table); // no CallScope
    Result<std::vector<SchemaNode>> rootNodes();
    Result<std::vector<SchemaNode>> folderItems(const QStringList &parts);
    Result<std::vector<SchemaNode>> tableItems(const QStringList &parts);

    ConnectionConfig m_config;
    std::shared_ptr<DbState> m_state;
};

} // namespace quarry::sqlite
