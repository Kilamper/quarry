#pragma once

#include <QStringList>

#include <cstdint>

namespace quarry {

enum class ObjectKind : uint8_t {
    Database,
    Schema,
    Table,
    View,
    MaterializedView,
    Column,
    Index,
    ForeignKey,
    Function,
    Procedure,
    Sequence,
    Trigger,
    Folder
};

// Identifies a tree object: [database, schema, table, ...] + kind.
struct ObjectPath
{
    QStringList parts;
    ObjectKind kind = ObjectKind::Table;
};

} // namespace quarry
