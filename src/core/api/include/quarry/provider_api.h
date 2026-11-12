#pragma once

#include <quarry/cancellation.h>
#include <quarry/object_path.h>
#include <quarry/result.h>
#include <quarry/result_batch.h>
#include <quarry/value_type.h>

#include <QFlags>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QVersionNumber>
#include <QtPlugin>

#include <memory>
#include <optional>
#include <vector>

namespace quarry {

inline constexpr int kProviderApiVersion = 1;

enum class Capability : uint32_t {
    Schemas = 1U << 0,
    MultipleDatabases = 1U << 1,
    Transactions = 1U << 2,
    Cancel = 1U << 3,
    Explain = 1U << 4,
    ServerSideCursor = 1U << 5,
    MultipleResultSets = 1U << 6,
    Ssh = 1U << 7
};
Q_DECLARE_FLAGS(Capabilities, Capability)
Q_DECLARE_OPERATORS_FOR_FLAGS(Capabilities)

struct ProviderInfo
{
    QString id; // "postgres"
    QString displayName; // "PostgreSQL"
    QVersionNumber version;
    Capabilities caps;
};

// Describes the connection form; the app builds it without knowing the engine.
struct ConnectionField
{
    enum class Kind { Text, Number, Path, Enum, Bool, Secret };

    QString key; // "host", "port", "database", "file", "sslmode"...
    QString label; // translatable
    Kind kind = Kind::Text;
    QVariant defaultValue;
    QStringList enumValues;
    bool required = false;
    bool advanced = false; // hidden under "Advanced options"
};

struct ConnectionConfig
{
    QUuid id;
    QVariantMap values; // no secrets
};

struct Secrets // comes from CredentialStore, never persisted
{
    QString password;
};

struct SchemaNode
{
    ObjectPath path;
    QString name;
    bool hasChildren = false; // draws the expander without loading children
    QString detail; // "int4 NOT NULL", "~12 345 rows"...
};

struct ColumnDef
{
    QString name;
    ValueType type = ValueType::Other;
    QString nativeType;
    bool nullable = true;
    QString defaultExpression;
};

struct IndexDef
{
    QString name;
    QStringList columns;
    bool unique = false;
};

struct ForeignKeyDef
{
    QString name;
    QStringList columns;
    ObjectPath referencedTable;
    QStringList referencedColumns;
};

struct TableDetails
{
    std::vector<ColumnDef> columns;
    std::vector<IndexDef> indexes;
    std::vector<ForeignKeyDef> foreignKeys;
    std::optional<QStringList> primaryKey;
};

struct ServerInfo
{
    QString product; // "PostgreSQL"
    QString version; // "18.1"
};

struct Statement
{
    QString sql;
    QVariantList parameters; // positional; each dialect translates placeholders
    int fetchSize = 500;
};

// Half-open range [begin, end) into the script text.
struct StatementRange
{
    qsizetype begin = 0;
    qsizetype end = 0;
};

enum class ExplainMode : uint8_t { Plan, Analyze };

class IResultStream
{
public:
    virtual ~IResultStream() = default;
    virtual const std::vector<ColumnInfo> &columns() const = 0; // empty if no rows are returned
    // Blocks until maxRows rows are available or the end is reached. An empty batch with
    // atEnd() == true marks the end. Must discard any pending cancel request on entry (see
    // IConnection::requestCancel()).
    virtual Result<ResultBatch> fetch(qsizetype maxRows, const CancellationToken &) = 0;
    virtual bool atEnd() const = 0;
    virtual std::optional<qint64> affectedRows() const = 0; // UPDATE/DELETE/INSERT
    virtual QStringList notices() const = 0; // RAISE NOTICE, warnings
    // Several result sets (SQL Server, MySQL CALL...). Returns false if there are no more.
    virtual Result<bool> nextResult(const CancellationToken &) = 0;
};

class IConnection
{
public:
    virtual ~IConnection() = default;
    virtual Result<ServerInfo> open(const CancellationToken &) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // On-demand introspection: only the direct children of `parent`.
    virtual Result<std::vector<SchemaNode>> children(const ObjectPath &parent,
                                                     const CancellationToken &)
        = 0;
    virtual Result<TableDetails> describe(const ObjectPath &table, const CancellationToken &) = 0;

    // Streaming execution. The stream keeps the connection busy until it is destroyed.
    virtual Result<std::unique_ptr<IResultStream>> execute(const Statement &,
                                                           const CancellationToken &)
        = 0;

    // Thread-safe: called from the UI thread while another thread is in execute()/fetch().
    // Must make the blocked call return soon with ErrorKind::Cancelled. It affects only the
    // call in flight: implementations must discard any pending cancel request at the start of
    // each open/children/describe/execute/fetch call, so a late cancel never aborts the next
    // statement.
    virtual void requestCancel() = 0;

    // Explicit transactions (milestone 2, data editing)
    virtual Result<void> begin() = 0;
    virtual Result<void> commit() = 0;
    virtual Result<void> rollback() = 0;
};

class ISqlDialect
{
public:
    virtual ~ISqlDialect() = default;
    virtual QString quoteIdentifier(QStringView) const = 0; // "a""b" | `a``b` | [a]]b]
    virtual QString quoteLiteral(QStringView) const = 0;
    virtual QString highlightingDefinition() const = 0; // KSyntaxHighlighting definition name
    virtual QStringList keywords() const = 0; // for autocompletion
    // Splits a script into statements: handles $$...$$, DELIMITER, comments and strings.
    virtual std::vector<StatementRange> splitStatements(QStringView script) const = 0;
    virtual QString limitClause(qint64 limit, qint64 offset) const = 0;
    virtual QString explainPrefix(ExplainMode) const = 0;
};

class IDatabaseProvider
{
public:
    virtual ~IDatabaseProvider() = default;
    virtual ProviderInfo info() const = 0;
    virtual std::vector<ConnectionField> connectionFields() const = 0;
    virtual const ISqlDialect &dialect() const = 0;
    // Creates a connection without opening it. Does not touch the network.
    virtual std::unique_ptr<IConnection> createConnection(const ConnectionConfig &,
                                                          const Secrets &) const
        = 0;
};

class IProviderPlugin
{
public:
    virtual ~IProviderPlugin() = default;
    virtual std::unique_ptr<IDatabaseProvider> createProvider() = 0;
};

} // namespace quarry

#define QUARRY_PROVIDER_IID "dev.quarry.IProviderPlugin/1"
Q_DECLARE_INTERFACE(quarry::IProviderPlugin, QUARRY_PROVIDER_IID)
