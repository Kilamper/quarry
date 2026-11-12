#pragma once

#include <quarry/column_buffer.h>
#include <quarry/object_path.h>
#include <quarry/value_type.h>

#include <QString>

#include <optional>
#include <vector>

namespace quarry {

struct ColumnInfo
{
    QString name;
    ValueType type = ValueType::Other; // normalized logical type; Other for mixed columns
    QString nativeType; // "int4", "varchar(255)", ...
    bool nullable = true;
    std::optional<ObjectPath> sourceTable; // needed for editing (milestone 2)
};

struct ResultBatch
{
    qsizetype rowCount = 0;
    std::vector<ColumnBuffer> columns; // one buffer per column
};

} // namespace quarry
