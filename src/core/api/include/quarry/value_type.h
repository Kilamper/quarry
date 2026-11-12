#pragma once

#include <cstdint>

namespace quarry {

enum class ValueType : uint8_t {
    Null,
    Bool,
    Int64,
    Double,
    Decimal,
    Text,
    Bytes,
    Date,
    Time,
    DateTime,
    DateTimeTz,
    Uuid,
    Json,
    Other
};

} // namespace quarry
