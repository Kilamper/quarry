#pragma once

#include <quarry/value_type.h>

#include <QByteArray>
#include <QByteArrayView>

#include <cstdint>
#include <vector>

namespace quarry {

// Columnar storage for one column of a batch. No QVariant per cell.
//
// Layout (one slot per row in each ACTIVE store, so row index == slot index):
//  - nulls bitmap : 1 bit/row, only grown when a NULL is appended.
//  - fixed        : int64 slot per row; Bool/Int64 stored directly, Double as its bit pattern.
//  - ends+arena   : bytes (Text, Decimal, Json, Bytes, Date, ...) are concatenated in `arena`;
//                   `ends[row]` is the uint32 end offset (start = ends[row-1]).
//  - tags         : only in mixed mode, 1 byte/row with the ValueType of each cell.
// A column starts typed with a single active store (fixed or bytes). If a value of another
// ValueType arrives it becomes mixed: both stores are active and tags are added (+1 B/cell,
// plus the unused slot of the other store). Columns with only NULLs so far have no store.
class ColumnBuffer
{
public:
    // Arena offsets are uint32: a batch column cannot hold more bytes than this.
    static constexpr qsizetype kMaxArenaBytes = UINT32_MAX;

    void reserve(qsizetype rows);

    void append(int64_t value);
    void append(double value);
    void appendBool(bool value);
    void appendNull();
    // `type` must be a bytes type (not Null/Bool/Int64/Double). If the arena would exceed
    // kMaxArenaBytes the cell is stored as NULL instead and false is returned.
    bool appendBytes(QByteArrayView value, ValueType type);

    qsizetype size() const noexcept { return m_size; }
    bool isMixed() const noexcept { return m_mixed; }
    bool isNull(qsizetype row) const;
    ValueType typeAt(qsizetype row) const;
    int64_t int64At(qsizetype row) const; // Int64 and Bool cells
    double doubleAt(qsizetype row) const;
    // View into the arena; valid until the next append (the arena may reallocate).
    QByteArrayView bytesAt(qsizetype row) const;

private:
    void prepare(ValueType type);
    void activate(bool fixed, bool bytes);
    void pushFixed(ValueType type, int64_t value);
    void finishRow(bool isNull, ValueType tag);

    qsizetype m_size = 0;
    qsizetype m_reserved = 0;
    ValueType m_type = ValueType::Null; // column type while typed; Null = nothing stored yet
    bool m_mixed = false;
    bool m_hasFixed = false;
    bool m_hasBytes = false;
    std::vector<uint64_t> m_nulls;
    std::vector<int64_t> m_fixed;
    std::vector<uint32_t> m_ends;
    QByteArray m_arena;
    std::vector<ValueType> m_tags;
};

} // namespace quarry
