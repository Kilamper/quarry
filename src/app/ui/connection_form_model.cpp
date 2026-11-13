#include "connection_form_model.h"

#include <algorithm>

namespace quarry::ui {

using Kind = quarry::ConnectionField::Kind;

ConnectionFormModel::ConnectionFormModel(QObject *parent)
    : QAbstractListModel(parent)
{ }

QString ConnectionFormModel::kindName(Kind kind)
{
    switch (kind) {
    case Kind::Number:
        return QStringLiteral("number");
    case Kind::Path:
        return QStringLiteral("path");
    case Kind::Enum:
        return QStringLiteral("enum");
    case Kind::Bool:
        return QStringLiteral("bool");
    case Kind::Text:
    case Kind::Secret:
        break;
    }
    return QStringLiteral("text");
}

void ConnectionFormModel::setFields(const std::vector<quarry::ConnectionField> &fields)
{
    beginResetModel();
    m_rows.clear();
    m_secretKeys.clear();
    for (const auto &f : fields) {
        if (f.kind == Kind::Secret) {
            m_secretKeys.append(f.key);
            continue;
        }
        Row row;
        row.field = f;
        if (f.kind == Kind::Bool)
            row.value = f.defaultValue.toBool();
        else if (f.kind == Kind::Enum)
            row.value = f.defaultValue.isValid()
                ? f.defaultValue.toString()
                : (f.enumValues.isEmpty() ? QString() : f.enumValues.constFirst());
        else
            row.value = f.defaultValue.toString();
        m_rows.push_back(std::move(row));
    }
    endResetModel();
    emit fieldsChanged();
    emit validChanged();
}

void ConnectionFormModel::setValues(const QVariantMap &values)
{
    for (auto &row : m_rows) {
        const auto it = values.constFind(row.field.key);
        if (it == values.constEnd())
            continue;
        row.value
            = row.field.kind == Kind::Bool ? QVariant(it->toBool()) : QVariant(it->toString());
    }
    if (!m_rows.empty())
        emit dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1));
    emit validChanged();
}

QVariantMap ConnectionFormModel::values() const
{
    QVariantMap out;
    for (const auto &row : m_rows) {
        const QString &key = row.field.key;
        switch (row.field.kind) {
        case Kind::Bool:
            out.insert(key, row.value.toBool());
            break;
        case Kind::Number: {
            const QString text = row.value.toString().trimmed();
            bool ok = false;
            const qlonglong n = text.toLongLong(&ok);
            if (ok)
                out.insert(key, n);
            break;
        }
        default: {
            const QString text = row.value.toString();
            if (!text.isEmpty())
                out.insert(key, text);
            break;
        }
        }
    }
    return out;
}

bool ConnectionFormModel::hasAdvanced() const
{
    for (const auto &row : m_rows)
        if (row.field.advanced)
            return true;
    return false;
}

void ConnectionFormModel::setValue(int row, const QVariant &value)
{
    if (row < 0 || row >= rowCount())
        return;
    auto &r = m_rows[static_cast<size_t>(row)];
    r.value = value;
    emit dataChanged(index(row), index(row), { ValueRole, ErrorRole });
    emit validChanged();
}

QString ConnectionFormModel::errorFor(const Row &row) const
{
    const QString text = row.value.toString().trimmed();
    if (row.field.kind == Kind::Number && !text.isEmpty()) {
        bool ok = false;
        text.toLongLong(&ok);
        if (!ok)
            return tr("Enter a whole number.");
    }
    const bool empty = row.field.kind != Kind::Bool && text.isEmpty();
    if (row.field.required && empty)
        return tr("This field is required.");
    return {};
}

bool ConnectionFormModel::isValid() const
{
    return std::ranges::all_of(m_rows, [this](const Row &r) { return errorFor(r).isEmpty(); });
}

int ConnectionFormModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant ConnectionFormModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const Row &row = m_rows[static_cast<size_t>(index.row())];
    switch (role) {
    case KeyRole:
        return row.field.key;
    case LabelRole:
        return row.field.label;
    case KindRole:
        return kindName(row.field.kind);
    case RequiredRole:
        return row.field.required;
    case AdvancedRole:
        return row.field.advanced;
    case EnumValuesRole:
        return row.field.enumValues;
    case ValueRole:
        return row.value;
    case ErrorRole:
        return errorFor(row);
    default:
        return {};
    }
}

QHash<int, QByteArray> ConnectionFormModel::roleNames() const
{
    return {
        { KeyRole, "key" },
        { LabelRole, "label" },
        { KindRole, "kind" },
        { RequiredRole, "isRequired" },
        { AdvancedRole, "isAdvanced" },
        { EnumValuesRole, "enumValues" },
        { ValueRole, "value" },
        { ErrorRole, "error" },
    };
}

} // namespace quarry::ui
