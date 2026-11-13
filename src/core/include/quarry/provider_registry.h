#pragma once

#include <quarry/connection_profile.h>
#include <quarry/provider_api.h>
#include <quarry/result.h>

#include <QPluginLoader>
#include <QString>
#include <QVersionNumber>

#include <map>
#include <memory>
#include <vector>

namespace quarry {

struct ProviderEntry
{
    QString id;
    QString displayName;
    QVersionNumber version;
    int apiVersion = 0;
    QString filePath;
};

struct RejectedPlugin
{
    QString filePath;
    QString reason;
};

class ProviderRegistry
{
public:
    ProviderRegistry();
    ~ProviderRegistry();
    ProviderRegistry(const ProviderRegistry &) = delete;
    ProviderRegistry &operator=(const ProviderRegistry &) = delete;

    // Reads embedded metadata only; never loads a library. Replaces previous results.
    void scan(const QString &directory);

    const std::vector<ProviderEntry> &available() const { return m_available; }
    const std::vector<RejectedPlugin> &rejected() const { return m_rejected; }

    // Loads the plugin on first use and caches the provider. Returns nullptr on failure.
    IDatabaseProvider *provider(const QString &id, QString *errorMessage = nullptr);
    bool isLoaded(const QString &id) const;

    // Loads the plugin if needed and creates an unopened connection for the profile.
    // Errors: NotFound (engine unavailable, detail = loader message), Internal (provider
    // returned no connection).
    Result<std::unique_ptr<IConnection>> createConnection(const ConnectionProfile &profile,
                                                          const Secrets &secrets);

    static QString defaultPluginDirectory();

private:
    struct Slot
    {
        std::unique_ptr<QPluginLoader> loader;
        // Declared after the loader so it is destroyed first.
        std::unique_ptr<IDatabaseProvider> provider;
    };

    void clear();

    std::vector<ProviderEntry> m_available;
    std::vector<RejectedPlugin> m_rejected;
    std::map<QString, Slot> m_slots;
};

} // namespace quarry
