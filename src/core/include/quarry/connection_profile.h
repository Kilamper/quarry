#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QUuid>
#include <QVariantMap>

#include <cstdint>
#include <optional>

namespace quarry {

enum class PasswordMode : uint8_t { None, Saved, AskEveryTime };

// A saved connection. `values` never holds secrets (ConnectionStore strips them).
struct ConnectionProfile
{
    QUuid id; // generated on first save if null
    QString name; // required, non-empty
    QString providerId; // "sqlite", "postgres"...
    QVariantMap values;
    int colorIndex = -1; // -1 = no color; 0..7 = palette in UX section 6
    bool production = false;
    PasswordMode passwordMode = PasswordMode::None;
    QDateTime createdAt, updatedAt; // UTC, set by ConnectionStore
    std::optional<QDateTime> lastUsedAt;
};

} // namespace quarry

Q_DECLARE_METATYPE(quarry::ConnectionProfile)
Q_DECLARE_METATYPE(QList<quarry::ConnectionProfile>)
