#include <quarry/column_buffer.h>

#include <QtGlobal>

#include <bit>

namespace quarry {

namespace {

bool isFixedType(ValueType type)
{
    return type == ValueType::Bool || type == ValueType::Int64 || type == ValueType::Double;
}

} // namespace

void ColumnBuffer::reserve(qsizetype rows)
{
    m_reserved = rows;
    const auto n = static_cast<size_t>(rows);
    if (m_hasFixed)
        m_fixed.reserve(n);
    if (m_hasBytes)
        m_ends.reserve(n);
    if (m_mixed)
        m_tags.reserve(n);
}

void ColumnBuffer::activate(bool fixed, bool bytes)
{
    // Backfill placeholders for rows appended before the store existed.
    if (fixed && !m_hasFixed) {
        m_hasFixed = true;
        m_fixed.reserve(static_cast<size_t>(m_reserved));
        m_fixed.resize(static_cast<size_t>(m_size), 0);
    }
    if (bytes && !m_hasBytes) {
        m_hasBytes = true;
        m_ends.reserve(static_cast<size_t>(m_reserved));
        m_ends.resize(static_cast<size_t>(m_size), 0);
    }
}

void ColumnBuffer::prepare(ValueType type)
{
    if (m_mixed || (m_type != ValueType::Null && type == m_type))
        return;
    if (m_type == ValueType::Null) {
        m_type = type;
        activate(isFixedType(type), !isFixedType(type));
        return;
    }
    m_mixed = true;
    m_tags.reserve(static_cast<size_t>(m_reserved));
    for (qsizetype row = 0; row < m_size; ++row)
        m_tags.push_back(isNull(row) ? ValueType::Null : m_type);
    activate(true, true);
}

void ColumnBuffer::finishRow(bool isNull, ValueType tag)
{
    if (m_mixed)
        m_tags.push_back(tag);
    if (isNull) {
        const auto word = static_cast<size_t>(m_size) / 64;
        if (m_nulls.size() <= word)
            m_nulls.resize(word + 1, 0);
        m_nulls[word] |= uint64_t(1) << (static_cast<size_t>(m_size) % 64);
    }
    ++m_size;
}

void ColumnBuffer::pushFixed(ValueType type, int64_t value)
{
    prepare(type);
    m_fixed.push_back(value);
    if (m_hasBytes)
        m_ends.push_back(static_cast<uint32_t>(m_arena.size()));
    finishRow(false, type);
}

void ColumnBuffer::append(int64_t value)
{
    pushFixed(ValueType::Int64, value);
}

void ColumnBuffer::append(double value)
{
    pushFixed(ValueType::Double, std::bit_cast<int64_t>(value));
}

void ColumnBuffer::appendBool(bool value)
{
    pushFixed(ValueType::Bool, value ? 1 : 0);
}

void ColumnBuffer::appendNull()
{
    if (m_hasFixed)
        m_fixed.push_back(0);
    if (m_hasBytes)
        m_ends.push_back(static_cast<uint32_t>(m_arena.size()));
    finishRow(true, ValueType::Null);
}

bool ColumnBuffer::appendBytes(QByteArrayView value, ValueType type)
{
    Q_ASSERT(!isFixedType(type) && type != ValueType::Null);
    if (m_arena.size() + value.size() > kMaxArenaBytes) {
        appendNull();
        return false;
    }
    prepare(type);
    m_arena.append(value);
    m_ends.push_back(static_cast<uint32_t>(m_arena.size()));
    if (m_hasFixed)
        m_fixed.push_back(0);
    finishRow(false, type);
    return true;
}

bool ColumnBuffer::isNull(qsizetype row) const
{
    Q_ASSERT(row >= 0 && row < m_size);
    const auto word = static_cast<size_t>(row) / 64;
    return word < m_nulls.size() && ((m_nulls[word] >> (static_cast<size_t>(row) % 64)) & 1) != 0;
}

ValueType ColumnBuffer::typeAt(qsizetype row) const
{
    if (m_mixed)
        return m_tags[static_cast<size_t>(row)];
    return isNull(row) ? ValueType::Null : m_type;
}

int64_t ColumnBuffer::int64At(qsizetype row) const
{
    Q_ASSERT(row >= 0 && row < m_size && m_hasFixed);
    return m_fixed[static_cast<size_t>(row)];
}

double ColumnBuffer::doubleAt(qsizetype row) const
{
    return std::bit_cast<double>(int64At(row));
}

QByteArrayView ColumnBuffer::bytesAt(qsizetype row) const
{
    Q_ASSERT(row >= 0 && row < m_size && m_hasBytes);
    const auto i = static_cast<size_t>(row);
    const qsizetype begin = i == 0 ? 0 : m_ends[i - 1];
    return QByteArrayView(m_arena.constData() + begin, qsizetype(m_ends[i]) - begin);
}

} // namespace quarry
