#include <quarry/provider_api.h>

#include <QObject>

namespace {

class DummyDialect : public quarry::ISqlDialect
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

class DummyProvider : public quarry::IDatabaseProvider
{
public:
    quarry::ProviderInfo info() const override
    {
        return { QStringLiteral("dummy"), QStringLiteral("Dummy"), QVersionNumber(0, 1, 0), {} };
    }
    std::vector<quarry::ConnectionField> connectionFields() const override { return {}; }
    const quarry::ISqlDialect &dialect() const override { return m_dialect; }
    std::unique_ptr<quarry::IConnection> createConnection(const quarry::ConnectionConfig &,
                                                          const quarry::Secrets &) const override
    {
        return nullptr;
    }

private:
    DummyDialect m_dialect;
};

} // namespace

class DummyPlugin : public QObject, public quarry::IProviderPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUARRY_PROVIDER_IID FILE "dummy.json")
    Q_INTERFACES(quarry::IProviderPlugin)

public:
    std::unique_ptr<quarry::IDatabaseProvider> createProvider() override
    {
        return std::make_unique<DummyProvider>();
    }
};

#include "dummy_plugin.moc"
