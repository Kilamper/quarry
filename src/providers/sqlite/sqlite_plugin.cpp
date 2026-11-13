#include "sqlite_provider.h"

#include <QObject>

class SqlitePlugin : public QObject, public quarry::IProviderPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUARRY_PROVIDER_IID FILE "sqlite.json")
    Q_INTERFACES(quarry::IProviderPlugin)

public:
    std::unique_ptr<quarry::IDatabaseProvider> createProvider() override
    {
        return std::make_unique<quarry::sqlite::SqliteProvider>();
    }
};

#include "sqlite_plugin.moc"
