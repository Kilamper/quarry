#pragma once

#include <quarry/editor_tab.h>
#include <quarry/editor_tab_store.h>

#include <QAbstractListModel>
#include <QString>
#include <QTimer>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace quarry {
class ISqlDialect;
}

namespace quarry::ui {

class ConnectionController;

// The open SQL editor tabs (ARCHITECTURE section 11.2): the list for the tab bar, the text of
// each tab kept in memory and saved to quarry.db through EditorTabStore, and the statement
// logic of the editor (current statement, what Mod+Enter would run) computed with the
// dialect's splitStatements().
//
// Saving: an edit schedules a save `saveDelay` ms after the last one; creating a tab, closing
// it, changing its connection and destroying the model write at once. Nothing is written
// before start() has loaded the stored tabs, except a tab the user opens meanwhile.
class EditorTabsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(
        QString databasePath READ databasePath WRITE setDatabasePath NOTIFY configChanged FINAL)
    // Gives the connection list (name, color, production) and the engines (dialect).
    Q_PROPERTY(quarry::ui::ConnectionController *connections READ connections WRITE setConnections
                   NOTIFY configChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(
        int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged FINAL)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged FINAL)
    Q_PROPERTY(int saveDelay READ saveDelay WRITE setSaveDelay NOTIFY configChanged FINAL)
    Q_PROPERTY(int splitDelay READ splitDelay WRITE setSplitDelay NOTIFY configChanged FINAL)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        ConnectionIdRole, // "" = none
        ConnectionNameRole,
        ColorIndexRole, // -1 = none
        ProductionRole,
        DefinitionRole, // KSyntaxHighlighting definition of the tab's dialect (loads the engine)
    };

    static constexpr int kDefaultSaveDelayMs = 1000;
    static constexpr int kDefaultSplitDelayMs = 250;

    explicit EditorTabsModel(QObject *parent = nullptr);
    // Writes what is pending; the store's destructor then finishes the queued writes.
    ~EditorTabsModel() override;

    QString databasePath() const { return m_databasePath; }
    void setDatabasePath(const QString &path);
    ConnectionController *connections() const { return m_connections; }
    void setConnections(ConnectionController *connections);
    int count() const { return static_cast<int>(m_tabs.size()); }
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int index);
    bool loaded() const { return m_loaded; }
    int saveDelay() const { return m_saveTimer.interval(); }
    void setSaveDelay(int ms) { m_saveTimer.setInterval(ms); }
    int splitDelay() const { return m_splitTimer.interval(); }
    void setSplitDelay(int ms) { m_splitTimer.setInterval(ms); }

    // Starts loading the stored tabs (creates the store and its thread). Idempotent. The app
    // calls it after the first frame.
    Q_INVOKABLE void start();

    // Opens a tab, makes it current and stores it. It gets the connection of the current tree
    // node (`treeConnectionId`, "" = none), else the one of the current tab, else none. Returns
    // its row.
    Q_INVOKABLE int openTab(const QString &treeConnectionId);
    Q_INVOKABLE void closeTab(int row); // deletes its stored text
    Q_INVOKABLE bool hasText(int row) const;
    Q_INVOKABLE void cycle(int step); // next (+1) / previous (-1), wrapping

    Q_INVOKABLE QString idOf(int row) const;
    Q_INVOKABLE QString titleOf(int row) const;
    Q_INVOKABLE QString connectionIdOf(int row) const;
    Q_INVOKABLE QString textOf(int row) const;
    Q_INVOKABLE int cursorOf(int row) const;
    Q_INVOKABLE void setText(int row, const QString &text);
    Q_INVOKABLE void setCursor(int row, int position);
    // "" = none. Stored at once.
    Q_INVOKABLE void setConnection(int row, const QString &connectionId);
    // KSyntaxHighlighting definition of the tab's dialect; "SQL" without a connection. Loads the
    // engine's plugin the first time (UI thread, a few ms).
    Q_INVOKABLE QString definitionOf(int row) const;

    // Statement under `position` using the last split: {index (-1 = none), count, begin, end
    // (UTF-16 offsets, end exclusive), startLine, endLine (1-based)}. Between statements it is
    // the previous one, or the first when there is none before.
    Q_INVOKABLE QVariantMap statementAt(int row, int position);
    // What Mod+Enter (all = false, the statement at `position`) and Mod+Shift+Enter (all = true)
    // would run: {ok, kind ("statement" | "all" | "none"), message (for the screen reader),
    // tabId, connectionId, index, count, ranges: [{begin, end, startLine, endLine}]}. Splits
    // fresh text, ignoring the debounce. With ok, runRequested() is emitted with the same map:
    // the hook H1.6 uses to execute those ranges.
    Q_INVOKABLE QVariantMap run(int row, bool all, int position);

    // Writes every tab with pending changes now.
    Q_INVOKABLE void flush();

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void configChanged();
    void countChanged();
    void currentIndexChanged();
    void loadedChanged();
    // The statements of a tab were split again (after an edit, or a connection change).
    void statementsChanged(int row);
    void runRequested(const QVariantMap &plan);
    // A store operation failed (message in plain language, technical detail apart).
    void failed(const QString &message, const QString &detail);

private:
    struct Statement
    {
        qsizetype begin = 0;
        qsizetype end = 0;
        int startLine = 1;
        int endLine = 1;
    };
    struct Tab
    {
        quarry::EditorTab data;
        bool dirty = false;
        bool computed = false; // statements holds a split (possibly of older text)
        bool stale = false; // edited since that split
        std::vector<Statement> statements;
    };

    bool valid(int row) const { return row >= 0 && row < count(); }
    void ensureStore();
    void saveNow(int row);
    void markDirty(int row);
    void renumber();
    void onLoaded(const QList<quarry::EditorTab> &tabs);
    void onStoreFailed(const quarry::Error &error);
    void onConnectionRemoved(const QString &id);
    void connectionsRefreshed();
    QString nextTitle() const;
    const quarry::ISqlDialect *dialectOf(const Tab &tab, QString *error = nullptr) const;
    void split(Tab &tab);
    QVariantMap rangeMap(const Statement &s) const;

    QString m_databasePath;
    ConnectionController *m_connections = nullptr; // cleared by its destroyed() signal
    quarry::EditorTabStore *m_store = nullptr;
    std::vector<Tab> m_tabs;
    int m_current = -1;
    bool m_started = false;
    bool m_loaded = false;
    QTimer m_saveTimer;
    QTimer m_splitTimer;
    mutable QHash<QString, QString> m_definitions; // engine id -> definition name
};

} // namespace quarry::ui
