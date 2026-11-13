#include <quarry/provider_api.h>

#include <QObject>
#include <QThread>

#include <atomic>

namespace {

class SlowDialect : public quarry::ISqlDialect
{
public:
    QString quoteIdentifier(QStringView name) const override { return name.toString(); }
    QString quoteLiteral(QStringView text) const override { return text.toString(); }
    QString highlightingDefinition() const override { return {}; }
    QStringList keywords() const override { return {}; }
    std::vector<quarry::StatementRange> splitStatements(QStringView) const override { return {}; }
    QString limitClause(qint64, qint64) const override { return {}; }
    QString explainPrefix(quarry::ExplainMode) const override { return {}; }
};

// open() blocks until it is cancelled (or 30 s pass, so a broken test cannot hang forever).
class SlowConnection : public quarry::IConnection
{
public:
    quarry::Result<quarry::ServerInfo> open(const quarry::CancellationToken &token) override
    {
        m_cancel = false;
        for (int i = 0; i < 3000 && !m_cancel && !token.isCancelled(); ++i)
            QThread::msleep(10);
        return quarry::fail(quarry::Error {
            quarry::ErrorKind::Cancelled, QStringLiteral("Cancelled."), {}, {}, {} });
    }
    void close() override { }
    bool isOpen() const override { return false; }
    quarry::Result<std::vector<quarry::SchemaNode>>
    children(const quarry::ObjectPath &, const quarry::CancellationToken &) override
    {
        return unsupported();
    }
    quarry::Result<quarry::TableDetails> describe(const quarry::ObjectPath &,
                                                  const quarry::CancellationToken &) override
    {
        return unsupported();
    }
    quarry::Result<std::unique_ptr<quarry::IResultStream>>
    execute(const quarry::Statement &, const quarry::CancellationToken &) override
    {
        return unsupported();
    }
    void requestCancel() override { m_cancel = true; }
    quarry::Result<void> begin() override { return unsupported(); }
    quarry::Result<void> commit() override { return unsupported(); }
    quarry::Result<void> rollback() override { return unsupported(); }

private:
    static quarry::Unexpected unsupported()
    {
        return quarry::fail(quarry::Error {
            quarry::ErrorKind::Unsupported, QStringLiteral("Unsupported."), {}, {}, {} });
    }
    std::atomic<bool> m_cancel { false };
};

class SlowProvider : public quarry::IDatabaseProvider
{
public:
    quarry::ProviderInfo info() const override
    {
        return { QStringLiteral("slow"), QStringLiteral("Slow"), QVersionNumber(0, 1, 0), {} };
    }
    std::vector<quarry::ConnectionField> connectionFields() const override { return {}; }
    const quarry::ISqlDialect &dialect() const override { return m_dialect; }
    std::unique_ptr<quarry::IConnection> createConnection(const quarry::ConnectionConfig &,
                                                          const quarry::Secrets &) const override
    {
        return std::make_unique<SlowConnection>();
    }

private:
    SlowDialect m_dialect;
};

} // namespace

class SlowPlugin : public QObject, public quarry::IProviderPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUARRY_PROVIDER_IID FILE "slow.json")
    Q_INTERFACES(quarry::IProviderPlugin)

public:
    std::unique_ptr<quarry::IDatabaseProvider> createProvider() override
    {
        return std::make_unique<SlowProvider>();
    }
};

#include "slow_plugin.moc"
