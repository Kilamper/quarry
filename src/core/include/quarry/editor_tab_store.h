#pragma once

#include <quarry/editor_tab.h>
#include <quarry/result.h>
// Declares the Q_DECLARE_METATYPE for quarry::Error used by failed().
#include <quarry/session.h>

#include <QList>
#include <QObject>

class QThread;

namespace quarry {

class EditorTabStoreWorker;

// UI-thread facade over the `editor_tabs` table of quarry.db. Same threading model as
// ConnectionStore: the sqlite handle lives on a dedicated worker thread (opened lazily), every
// method returns at once and the outcome arrives as a queued signal, in call order.
class EditorTabStore : public QObject
{
    Q_OBJECT

public:
    explicit EditorTabStore(QString databasePath, QObject *parent = nullptr);
    // Finishes the queued operations, closes the database and joins the thread (up to 5 s).
    ~EditorTabStore() override;

public slots:
    void load(); // -> loaded(tabs ordered by position, then updatedAt, then id) | failed(Error)
    // Inserts or updates by id (a null id gets a new UUID). Needs a non-empty title; a
    // connectionId that matches no saved connection fails with ErrorKind::Constraint.
    // updatedAt of the argument is ignored. -> saved(tab as stored) | failed(Error)
    void save(quarry::EditorTab tab);
    void remove(QUuid id); // -> removed(id) (also if it did not exist) | failed(Error)

signals:
    void loaded(QList<quarry::EditorTab> tabs);
    void saved(quarry::EditorTab tab);
    void removed(QUuid id);
    void failed(quarry::Error error); // not tied to an operation: the UI shows it as is

private:
    template<typename F> void post(F &&task);

    QThread *m_thread = nullptr;
    EditorTabStoreWorker *m_worker = nullptr; // lives on m_thread, deleted there
};

} // namespace quarry
