#include <quarry/provider_api.h>

#include <QObject>

// Declares a future interface version, so the registry must reject it from metadata alone.
class DummyFuturePlugin : public QObject
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "dev.quarry.IProviderPlugin/2" FILE "dummy_future.json")
};

#include "dummy_future_plugin.moc"
