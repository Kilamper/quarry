#pragma once

#include "command.h"

#include <QAbstractListModel>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace quarry::ui {

// Single source of commands for menus, shortcuts and the palette. As a list model it exposes
// the commands matching `filter`, best match first (palette); `commands` lists all of them.
// Commands are not owned: one that is destroyed unregisters itself.
class CommandRegistry : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged FINAL)
    Q_PROPERTY(QVariantList commands READ commandList NOTIFY commandsChanged FINAL)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        CategoryRole,
        ShortcutRole,
        EnabledRole,
        CheckableRole,
        CheckedRole,
    };

    static constexpr int kMaxRecents = 10;

    explicit CommandRegistry(QObject *parent = nullptr);

    // False (and a warning) for a null command, an empty id or a duplicate id.
    Q_INVOKABLE bool registerCommand(quarry::ui::Command *command);
    Q_INVOKABLE bool unregisterCommand(const QString &id);
    Q_INVOKABLE quarry::ui::Command *find(const QString &id) const;
    // Runs an enabled command and records it as recent. False if unknown or disabled.
    Q_INVOKABLE bool trigger(const QString &id);

    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    QVariantList commandList() const;
    QStringList recents() const { return m_recents; }

    // 0 = no match; prefix > word start > subsequence. Case-insensitive.
    static int matchScore(const QString &query, const QString &text);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void filterChanged();
    void commandsChanged();

private:
    void rebuild();
    int rank(const Command *c) const;

    QList<Command *> m_all; // registration order
    QList<Command *> m_rows; // filtered and ranked
    QString m_filter;
    QStringList m_recents; // most recent first
};

} // namespace quarry::ui
