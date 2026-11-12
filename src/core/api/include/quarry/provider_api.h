#pragma once

#include <QString>
#include <QVersionNumber>
#include <QtPlugin>

#include <memory>

namespace quarry {

inline constexpr int kProviderApiVersion = 1;

struct ProviderInfo
{
    QString id;
    QString displayName;
    QVersionNumber version;
};

class IDatabaseProvider
{
public:
    virtual ~IDatabaseProvider() = default;
    virtual ProviderInfo info() const = 0;
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
