#include "editor_tab_store_worker.h"

#include <QLoggingCategory>
#include <QThread>

namespace quarry {

Q_LOGGING_CATEGORY(lcEditorTabStore, "quarry.editortabstore")

namespace {
constexpr int kShutdownTimeoutMs = 5000;
}

EditorTabStore::EditorTabStore(QString databasePath, QObject *parent)
    : QObject(parent)
    , m_thread(new QThread) // no parent: must outlive this object if the join times out
{
    qRegisterMetaType<quarry::Error>();
    qRegisterMetaType<quarry::EditorTab>();
    qRegisterMetaType<QList<quarry::EditorTab>>();

    m_worker = new EditorTabStoreWorker(std::move(databasePath));
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    const auto q = Qt::QueuedConnection;
    connect(m_worker, &EditorTabStoreWorker::loaded, this, &EditorTabStore::loaded, q);
    connect(m_worker, &EditorTabStoreWorker::saved, this, &EditorTabStore::saved, q);
    connect(m_worker, &EditorTabStoreWorker::removed, this, &EditorTabStore::removed, q);
    connect(m_worker, &EditorTabStoreWorker::failed, this, &EditorTabStore::failed, q);
    m_thread->start();
}

EditorTabStore::~EditorTabStore()
{
    // Queued after every pending operation, so nothing already requested is lost.
    post([w = m_worker] {
        w->closeDatabase();
        QThread::currentThread()->quit();
    });
    if (m_thread->wait(kShutdownTimeoutMs)) {
        delete m_thread;
    } else {
        qCWarning(lcEditorTabStore) << "Store thread did not stop within" << kShutdownTimeoutMs
                                    << "ms; leaking it until the disk call returns";
        connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    }
}

template<typename F> void EditorTabStore::post(F &&task)
{
    QMetaObject::invokeMethod(m_worker, std::forward<F>(task), Qt::QueuedConnection);
}

void EditorTabStore::load()
{
    post([w = m_worker] { w->load(); });
}

void EditorTabStore::save(EditorTab tab)
{
    post([w = m_worker, tab = std::move(tab)] { w->save(tab); });
}

void EditorTabStore::remove(QUuid id)
{
    post([w = m_worker, id] { w->remove(id); });
}

} // namespace quarry
