#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace quarry::ui {

// The Messages tab of one editor tab: one entry per statement result, notice, error or
// cancellation of the last run. Cleared when the next run starts.
class QueryMessagesModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role {
        KindRole = Qt::UserRole + 1, // "statement" | "notice" | "error" | "cancelled"
        TypeRole, // the kind in words, so it never relies on the icon or the color alone
        TextRole,
        DetailRole, // technical text (errors)
        LineRole, // 1-based line in the tab's text, 0 = unknown
        ColumnRole,
    };
    Q_ENUM(Role)

    struct Entry
    {
        QString kind;
        QString type;
        QString text;
        QString detail;
        int line = 0;
        int column = 0;
    };

    explicit QueryMessagesModel(QObject *parent = nullptr)
        : QAbstractListModel(parent)
    { }

    int count() const { return static_cast<int>(m_entries.size()); }
    void append(Entry entry);
    void clear();

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    QList<Entry> m_entries;
};

} // namespace quarry::ui
