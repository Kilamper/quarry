#pragma once

#include "connection_list_model.h"

#include <quarry/object_path.h>
#include <quarry/provider_api.h>
#include <quarry/provider_registry.h>
#include <quarry/result.h>
#include <quarry/session.h>

#include <QAbstractItemModel>
#include <QHash>
#include <QStringList>
#include <QUuid>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace quarry::ui {

// What the tree needs from the keychain and the per-session password memory. The controller
// wires it to CredentialStore (or to the test backend) and forwards the results to
// onPasswordRead() / onPasswordFailed().
struct SchemaCredentials
{
    std::function<void(QUuid)> read;
    std::function<std::optional<QString>(QUuid)> session;
    std::function<void(QUuid, QString)> remember;
    std::function<void(QUuid)> forget;
};

// The sidebar tree. Top level = the saved connections (mirrors ConnectionListModel, which stays
// the single source of truth for name, color, production...). Expanding a connection opens its
// metadata Session and lists its children; every other node loads its children when expanded
// (canFetchMore/fetchMore -> Session::children). While a request is pending the node has one
// synthetic "Loading…" child; on failure one synthetic error child (name = message, detail =
// technical text); a node that answers with no children gets one synthetic "(empty)" child, so
// a screen reader never says "expanded" about a node with nothing under it. Nodes the provider
// reports without children (leaves) have no expander at all.
class SchemaTreeModel : public QAbstractItemModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ConnectionController")

    // Ids of the connections whose session is open.
    Q_PROPERTY(QStringList connectedIds READ connectedIds NOTIFY connectedChanged FINAL)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        KindRole, // "connection", "database", "table"... or "loading", "error", "empty"
        DetailRole,
        ConnectionIdRole,
        ProviderNameRole,
        SummaryRole,
        ColorIndexRole,
        ColorNameRole,
        ProductionRole,
        ConnectedRole, // connection nodes: the session is open
        LoadStateRole, // LoadState
        PositionRole, // 1-based position among the siblings
        SetSizeRole, // number of siblings
    };
    Q_ENUM(Role)

    enum LoadState { Idle, Loading, Loaded, Failed };
    Q_ENUM(LoadState)

    explicit SchemaTreeModel(ConnectionListModel *connections, quarry::ProviderRegistry *registry,
                             QObject *parent = nullptr);
    // Closes every session.
    ~SchemaTreeModel() override;

    void setCredentials(SchemaCredentials credentials) { m_credentials = std::move(credentials); }
    QStringList connectedIds() const;

    // Keychain results (forwarded by the controller). onPasswordFailed returns true when the
    // tree was waiting for that read.
    void onPasswordRead(QUuid id, const QString &password);
    bool onPasswordFailed(QUuid id, const quarry::Error &error);

    // The tree and the controller are cheap to ask: row of a connection node, or -1.
    Q_INVOKABLE QModelIndex indexOfConnection(const QString &connectionId) const;
    // The connection node at the top of `index`'s branch (invalid for an invalid index).
    Q_INVOKABLE QModelIndex connectionOf(const QModelIndex &index) const;
    Q_INVOKABLE QString connectionIdOf(const QModelIndex &index) const;
    Q_INVOKABLE bool isConnected(const QString &connectionId) const;
    // Error row or its parent: loads the parent's children again (and reconnects a connection).
    Q_INVOKABLE void retry(const QModelIndex &index);
    // Closes the session and returns the connection's subtree to "not loaded".
    Q_INVOKABLE void disconnectConnection(const QString &connectionId);
    // Answer to passwordRequested(). An empty password is allowed.
    Q_INVOKABLE void providePassword(const QString &connectionId, const QString &password);
    // The user dismissed the password prompt.
    Q_INVOKABLE void cancelPassword(const QString &connectionId);

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    bool hasChildren(const QModelIndex &parent = {}) const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void connectedChanged();
    // For the screen reader, without moving the focus.
    void announcement(const QString &text, bool assertive);
    // The node must be shown collapsed (disconnected, or the password prompt was dismissed).
    void collapseRequested(const QModelIndex &index);
    // "Ask every time" and no password remembered for this run. `retry` = the last one failed.
    void passwordRequested(const QString &connectionId, const QString &name, bool retry);

private:
    struct Node;
    struct ConnData;

    Node *node(const QModelIndex &index) const;
    QModelIndex indexFor(const Node *n) const;
    Node *rootOf(Node *n) const;
    bool attached(const Node *root) const;
    void renumberRoots();
    std::unique_ptr<Node> makeRoot(const QUuid &id) const;

    void fillRoots();
    void onSourceInserted(int first, int last);
    void onSourceRemoved(int first, int last);
    void onSourceChanged(int first, int last);
    void onSourceReset();
    void flushParked();

    void setChildren(Node *n, std::vector<std::unique_ptr<Node>> kids);
    void setState(Node *n, LoadState state);
    // fetchMore() only marks the node and queues the work: a view (or QAbstractItemModelTester)
    // may call it from inside a model-change handler, where changing the model again is illegal.
    void defer(Node *n);
    static std::unique_ptr<Node> makeObject(quarry::SchemaNode &&sn);
    void queueDrain();
    void drainDeferred();
    void load(Node *n);
    void connectRoot(Node *root);
    void proceedConnect(Node *root, const QString &password);
    void openSession(Node *root, std::unique_ptr<quarry::IConnection> connection);
    void failNode(Node *n, const quarry::Error &error);
    void disposeSession(ConnData &conn);
    void dropRequests(quint64 token);
    void setConnected(Node *root, bool connected);
    void onChildrenReady(quint64 token, quint64 requestId, std::vector<quarry::SchemaNode> nodes);
    void onChildrenFailed(quint64 token, quint64 requestId, const quarry::Error &error);
    void onSessionOpened(quint64 token);
    void onSessionFailed(quint64 token, const quarry::Error &error);
    Node *rootById(const QUuid &id) const;
    Node *rootByToken(quint64 token) const;
    QVariant rootData(const Node *root, int role) const;

    ConnectionListModel *m_list;
    quarry::ProviderRegistry *m_registry;
    SchemaCredentials m_credentials;
    std::vector<std::unique_ptr<Node>> m_roots;
    // Roots whose row was removed: a rename moves the row (remove + insert), and the open
    // session must survive that. Whatever nobody claimed by the next event-loop turn is closed.
    std::vector<std::unique_ptr<Node>> m_parked;
    // (session token, request id) -> node with a request in flight. Entries of a closed session
    // are erased, so a late answer finds nothing and is ignored.
    QHash<std::pair<quint64, quint64>, Node *> m_requests;
    quint64 m_nextToken = 0;
    bool m_drainQueued = false;
};

} // namespace quarry::ui
