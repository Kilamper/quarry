#pragma once

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QUuid>

#include <optional>

namespace quarry {

// An open SQL editor tab, persisted in quarry.db so typed text is never lost.
struct EditorTab
{
    QUuid id; // null = new; EditorTabStore generates one on first save
    std::optional<QUuid> connectionId; // nullopt = tab without a connection
    QString title; // "Query 1"...; non-empty
    QString text;
    int cursorPosition = 0;
    int position = 0; // order in the tab bar
    QDateTime updatedAt; // UTC, set by EditorTabStore
};

} // namespace quarry

Q_DECLARE_METATYPE(quarry::EditorTab)
Q_DECLARE_METATYPE(QList<quarry::EditorTab>)
