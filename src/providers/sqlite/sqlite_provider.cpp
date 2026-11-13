#include "sqlite_provider.h"

#include "sqlite_connection.h"
#include "sqlite_text.h"

namespace quarry::sqlite {

ProviderInfo SqliteProvider::info() const
{
    return { QStringLiteral("sqlite"), QStringLiteral("SQLite"), QVersionNumber(0, 1, 0),
             Capability::MultipleDatabases | Capability::Transactions | Capability::Cancel
                 | Capability::Explain };
}

std::vector<ConnectionField> SqliteProvider::connectionFields() const
{
    ConnectionField file;
    file.key = QStringLiteral("file");
    file.label = Text::tr("Database file (or :memory:)");
    file.kind = ConnectionField::Kind::Path;
    file.required = true;

    ConnectionField readOnly;
    readOnly.key = QStringLiteral("readOnly");
    readOnly.label = Text::tr("Open read-only");
    readOnly.kind = ConnectionField::Kind::Bool;
    readOnly.defaultValue = false;
    readOnly.advanced = true;

    ConnectionField busy;
    busy.key = QStringLiteral("busyTimeoutMs");
    busy.label = Text::tr("Busy timeout (ms)");
    busy.kind = ConnectionField::Kind::Number;
    busy.defaultValue = 5000;
    busy.advanced = true;

    return { file, readOnly, busy };
}

std::unique_ptr<IConnection> SqliteProvider::createConnection(const ConnectionConfig &config,
                                                              const Secrets &) const
{
    return std::make_unique<SqliteConnection>(config);
}

} // namespace quarry::sqlite
