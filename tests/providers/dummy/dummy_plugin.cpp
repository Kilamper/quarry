#include <quarry/provider_api.h>

#include <QObject>

namespace {

class DummyProvider : public quarry::IDatabaseProvider
{
public:
    quarry::ProviderInfo info() const override
    {
        return { QStringLiteral("dummy"), QStringLiteral("Dummy"), QVersionNumber(0, 1, 0) };
    }
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
