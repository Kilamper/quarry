#include "query_messages_model.h"

namespace quarry::ui {

void QueryMessagesModel::append(Entry entry)
{
    const int row = count();
    beginInsertRows({}, row, row);
    m_entries.push_back(std::move(entry));
    endInsertRows();
    emit countChanged();
}

void QueryMessagesModel::clear()
{
    if (m_entries.isEmpty())
        return;
    beginResetModel();
    m_entries.clear();
    endResetModel();
    emit countChanged();
}

int QueryMessagesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant QueryMessagesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count())
        return {};
    const Entry &e = m_entries.at(index.row());
    switch (role) {
    case KindRole:
        return e.kind;
    case TypeRole:
        return e.type;
    case TextRole:
        return e.text;
    case DetailRole:
        return e.detail;
    case LineRole:
        return e.line;
    case ColumnRole:
        return e.column;
    default:
        return {};
    }
}

QHash<int, QByteArray> QueryMessagesModel::roleNames() const
{
    return {
        { KindRole, "kind" },     { TypeRole, "type" }, { TextRole, "text" },
        { DetailRole, "detail" }, { LineRole, "line" }, { ColumnRole, "column" },
    };
}

} // namespace quarry::ui
