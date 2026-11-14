#pragma once

#include "app_database.h"

#include <quarry/editor_tab_store.h>

#include <QObject>

namespace quarry {

// Private to EditorTabStore. Lives on the store thread and owns the sqlite3 handle.
class EditorTabStoreWorker : public QObject
{
    Q_OBJECT

public:
    explicit EditorTabStoreWorker(QString databasePath);

    void load();
    void save(const EditorTab &tab);
    void remove(const QUuid &id);
    void closeDatabase();

signals:
    void loaded(QList<quarry::EditorTab> tabs);
    void saved(quarry::EditorTab tab);
    void removed(QUuid id);
    void failed(quarry::Error error);

private:
    AppDatabase m_database;
};

} // namespace quarry
