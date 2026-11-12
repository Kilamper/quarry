#include <quarry/provider_registry.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QLibrary>
#include <QLoggingCategory>

#include <algorithm>

namespace quarry {

Q_LOGGING_CATEGORY(lcRegistry, "quarry.registry")

ProviderRegistry::ProviderRegistry() = default;

ProviderRegistry::~ProviderRegistry()
{
    clear();
}

void ProviderRegistry::clear()
{
    // Instances must die before their loader unloads the library that holds their vtable.
    for (auto &entry : m_slots)
        entry.second.provider.reset();
    m_slots.clear();
    m_available.clear();
    m_rejected.clear();
}

void ProviderRegistry::scan(const QString &directory)
{
    clear();

    const QFileInfoList files = QDir(directory).entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo &file : files) {
        const QString path = file.absoluteFilePath();
        if (!QLibrary::isLibrary(path))
            continue;

        auto reject = [&](const QString &reason) {
            qWarning(lcRegistry).noquote() << "Rejected plugin" << path << ":" << reason;
            m_rejected.push_back({ path, reason });
        };

        auto loader = std::make_unique<QPluginLoader>(path);
        const QJsonObject root = loader->metaData();
        if (root.isEmpty()) {
            reject(QStringLiteral("The file is not a Qt plugin or has no embedded metadata."));
            continue;
        }
        const QString iid = root.value(QLatin1String("IID")).toString();
        if (iid != QLatin1String(QUARRY_PROVIDER_IID)) {
            reject(QStringLiteral("Unexpected plugin interface \"%1\"; the API version is "
                                  "incompatible or this is not a Quarry provider.")
                       .arg(iid));
            continue;
        }
        const QJsonObject meta = root.value(QLatin1String("MetaData")).toObject();
        const int apiVersion = meta.value(QLatin1String("apiVersion")).toInt(-1);
        if (apiVersion != kProviderApiVersion) {
            reject(QStringLiteral("Unsupported API version %1; this build requires version %2.")
                       .arg(apiVersion)
                       .arg(kProviderApiVersion));
            continue;
        }
        const QString id = meta.value(QLatin1String("id")).toString();
        const QString name = meta.value(QLatin1String("name")).toString();
        if (id.isEmpty() || name.isEmpty()) {
            reject(QStringLiteral("The plugin metadata is missing \"id\" or \"name\"."));
            continue;
        }
        if (m_slots.count(id) != 0) {
            reject(
                QStringLiteral("Duplicate provider id \"%1\"; an earlier plugin already uses it.")
                    .arg(id));
            continue;
        }

        const QString version = meta.value(QLatin1String("version")).toString();
        m_available.push_back({ id, name, QVersionNumber::fromString(version), apiVersion, path });
        m_slots[id].loader = std::move(loader);
    }

    std::sort(m_available.begin(), m_available.end(),
              [](const ProviderEntry &a, const ProviderEntry &b) {
                  return a.displayName.compare(b.displayName, Qt::CaseInsensitive) < 0;
              });
}

IDatabaseProvider *ProviderRegistry::provider(const QString &id, QString *errorMessage)
{
    auto fail = [&](const QString &message) -> IDatabaseProvider * {
        if (errorMessage)
            *errorMessage = message;
        return nullptr;
    };

    const auto it = m_slots.find(id);
    if (it == m_slots.end())
        return fail(QStringLiteral("No compatible provider with id \"%1\" is available.").arg(id));

    Slot &slot = it->second;
    if (slot.provider)
        return slot.provider.get();

    QObject *instance = slot.loader->instance();
    if (!instance)
        return fail(QStringLiteral("Could not load the provider plugin: %1")
                        .arg(slot.loader->errorString()));
    auto *plugin = qobject_cast<IProviderPlugin *>(instance);
    if (!plugin)
        return fail(QStringLiteral("The plugin does not implement the provider interface."));

    slot.provider = plugin->createProvider();
    if (!slot.provider)
        return fail(QStringLiteral("The plugin failed to create its provider."));
    return slot.provider.get();
}

bool ProviderRegistry::isLoaded(const QString &id) const
{
    const auto it = m_slots.find(id);
    return it != m_slots.end() && it->second.provider != nullptr;
}

QString ProviderRegistry::defaultPluginDirectory()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
    if (exeDir.endsWith(QLatin1String("/Contents/MacOS")))
        return QDir::cleanPath(exeDir + QLatin1String("/../PlugIns/quarry"));
#endif
    return exeDir + QLatin1String("/plugins/quarry");
}

} // namespace quarry
