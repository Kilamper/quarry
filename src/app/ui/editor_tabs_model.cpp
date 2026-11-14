#include "editor_tabs_model.h"

#include "connection_controller.h"

#include <quarry/connection_store.h>
#include <quarry/provider_api.h>

#include <QSet>

#include <algorithm>
#include <iterator>

namespace quarry::ui {

namespace {

QString toId(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

} // namespace

EditorTabsModel::EditorTabsModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(kDefaultSaveDelayMs);
    connect(&m_saveTimer, &QTimer::timeout, this, &EditorTabsModel::flush);
    m_splitTimer.setSingleShot(true);
    m_splitTimer.setInterval(kDefaultSplitDelayMs);
    connect(&m_splitTimer, &QTimer::timeout, this, [this] {
        for (int i = 0; i < count(); ++i) {
            Tab &tab = m_tabs[static_cast<std::size_t>(i)];
            if (tab.stale) {
                split(tab);
                emit statementsChanged(i);
            }
        }
    });
}

EditorTabsModel::~EditorTabsModel()
{
    flush(); // the store (a child, destroyed next) finishes the queued writes
}

void EditorTabsModel::setDatabasePath(const QString &path)
{
    if (path == m_databasePath)
        return;
    m_databasePath = path;
    emit configChanged();
}

void EditorTabsModel::setConnections(ConnectionController *connections)
{
    if (connections == m_connections)
        return;
    if (m_connections) {
        m_connections->disconnect(this);
        m_connections->model()->disconnect(this);
    }
    m_connections = connections;
    if (m_connections) {
        ConnectionListModel *list = m_connections->model();
        connect(list, &ConnectionListModel::modelReset, this,
                &EditorTabsModel::connectionsRefreshed);
        connect(list, &ConnectionListModel::countChanged, this,
                &EditorTabsModel::connectionsRefreshed);
        connect(list, &ConnectionListModel::dataChanged, this,
                &EditorTabsModel::connectionsRefreshed);
        connect(list, &ConnectionListModel::rowsMoved, this,
                &EditorTabsModel::connectionsRefreshed);
        connect(m_connections, &ConnectionController::connectionRemoved, this,
                &EditorTabsModel::onConnectionRemoved);
        connect(m_connections, &QObject::destroyed, this, [this] { m_connections = nullptr; });
    }
    emit configChanged();
}

// ---- model -------------------------------------------------------------------------------------

int EditorTabsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant EditorTabsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !valid(index.row()))
        return {};
    const Tab &tab = m_tabs[static_cast<std::size_t>(index.row())];
    QString connection = tab.data.connectionId ? toId(*tab.data.connectionId) : QString();
    switch (role) {
    case IdRole:
        return toId(tab.data.id);
    case TitleRole:
        return tab.data.title;
    case ConnectionIdRole:
        return connection;
    case DefinitionRole:
        return definitionOf(index.row());
    case ConnectionNameRole:
    case ColorIndexRole:
    case ProductionRole: {
        const int row = (m_connections && !connection.isEmpty())
            ? m_connections->model()->indexOf(connection)
            : -1;
        if (row < 0)
            return role == ColorIndexRole ? QVariant(-1)
                : role == ProductionRole  ? QVariant(false)
                                          : QVariant(QString());
        const auto list = m_connections->model();
        return list->data(list->index(row),
                          role == ConnectionNameRole   ? ConnectionListModel::NameRole
                              : role == ColorIndexRole ? ConnectionListModel::ColorIndexRole
                                                       : ConnectionListModel::ProductionRole);
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> EditorTabsModel::roleNames() const
{
    return {
        { IdRole, "tabId" },
        { TitleRole, "title" },
        { ConnectionIdRole, "connectionId" },
        { ConnectionNameRole, "connectionName" },
        { ColorIndexRole, "colorIndex" },
        { ProductionRole, "production" },
        { DefinitionRole, "definition" },
    };
}

void EditorTabsModel::connectionsRefreshed()
{
    if (m_tabs.empty())
        return;
    emit dataChanged(index(0), index(count() - 1),
                     { ConnectionNameRole, ColorIndexRole, ProductionRole, DefinitionRole });
    // The engine may have become reachable (or not) since the statements were split.
    for (Tab &tab : m_tabs)
        tab.computed = false;
    if (valid(m_current))
        emit statementsChanged(m_current);
}

void EditorTabsModel::onConnectionRemoved(const QString &id)
{
    const QUuid uuid = QUuid::fromString(id);
    for (int i = 0; i < count(); ++i) {
        Tab &tab = m_tabs[static_cast<std::size_t>(i)];
        if (tab.data.connectionId != uuid)
            continue;
        // quarry.db already forgot it (ON DELETE SET NULL); saving it again would be refused.
        tab.data.connectionId.reset();
        tab.computed = false;
        emit dataChanged(index(i), index(i),
                         { ConnectionIdRole, ConnectionNameRole, ColorIndexRole, ProductionRole,
                           DefinitionRole });
        emit statementsChanged(i);
    }
}

// ---- store -------------------------------------------------------------------------------------

void EditorTabsModel::ensureStore()
{
    if (m_store)
        return;
    m_store = new EditorTabStore(
        m_databasePath.isEmpty() ? ConnectionStore::defaultDatabasePath() : m_databasePath, this);
    connect(m_store, &EditorTabStore::loaded, this, &EditorTabsModel::onLoaded);
    connect(m_store, &EditorTabStore::failed, this, &EditorTabsModel::onStoreFailed);
}

void EditorTabsModel::start()
{
    if (m_started)
        return;
    m_started = true;
    ensureStore();
    m_store->load();
}

void EditorTabsModel::onLoaded(const QList<EditorTab> &stored)
{
    if (m_loaded)
        return;
    m_loaded = true;
    // A tab the user opened before the load ended is already in the list (and in the store).
    std::vector<Tab> restored;
    for (const EditorTab &t : stored) {
        const bool known
            = std::ranges::any_of(m_tabs, [&](const Tab &o) { return o.data.id == t.id; });
        if (!known) {
            Tab tab;
            tab.data = t;
            restored.push_back(std::move(tab));
        }
    }
    if (!restored.empty()) {
        const bool hadTabs = !m_tabs.empty();
        const int n = static_cast<int>(restored.size());
        beginInsertRows({}, 0, n - 1);
        m_tabs.insert(m_tabs.begin(), std::make_move_iterator(restored.begin()),
                      std::make_move_iterator(restored.end()));
        endInsertRows();
        emit countChanged();
        m_current = hadTabs ? m_current + n : 0;
        if (hadTabs)
            renumber();
        emit currentIndexChanged();
    }
    emit loadedChanged();
}

void EditorTabsModel::onStoreFailed(const Error &error)
{
    emit failed(error.message, error.detail);
}

void EditorTabsModel::saveNow(int row)
{
    if (!valid(row))
        return;
    ensureStore();
    Tab &tab = m_tabs[static_cast<std::size_t>(row)];
    tab.data.position = row;
    tab.dirty = false;
    m_store->save(tab.data);
}

void EditorTabsModel::markDirty(int row)
{
    m_tabs[static_cast<std::size_t>(row)].dirty = true;
    m_saveTimer.start();
}

void EditorTabsModel::flush()
{
    m_saveTimer.stop();
    for (int i = 0; i < count(); ++i)
        if (m_tabs[static_cast<std::size_t>(i)].dirty)
            saveNow(i);
}

// Positions follow the rows; the tabs that moved are saved with the next batch.
void EditorTabsModel::renumber()
{
    for (int i = 0; i < count(); ++i) {
        Tab &tab = m_tabs[static_cast<std::size_t>(i)];
        if (tab.data.position != i) {
            tab.data.position = i;
            markDirty(i);
        }
    }
}

// ---- tabs --------------------------------------------------------------------------------------

QString EditorTabsModel::nextTitle() const
{
    QSet<QString> used;
    for (const Tab &tab : m_tabs)
        used.insert(tab.data.title);
    for (int n = 1;; ++n) {
        const QString title = tr("Query %1").arg(n);
        if (!used.contains(title))
            return title;
    }
}

int EditorTabsModel::openTab(const QString &treeConnectionId)
{
    std::optional<QUuid> connection;
    if (!treeConnectionId.isEmpty()) {
        const QUuid id = QUuid::fromString(treeConnectionId);
        if (!id.isNull()
            && (!m_connections || m_connections->model()->indexOf(treeConnectionId) >= 0))
            connection = id;
    } else if (valid(m_current)) {
        connection = m_tabs[static_cast<std::size_t>(m_current)].data.connectionId;
    }

    Tab tab;
    tab.data.id = QUuid::createUuid();
    tab.data.connectionId = connection;
    tab.data.title = nextTitle();
    tab.data.position = count();
    const int row = count();
    beginInsertRows({}, row, row);
    m_tabs.push_back(std::move(tab));
    endInsertRows();
    emit countChanged();
    saveNow(row);
    setCurrentIndex(row);
    return row;
}

void EditorTabsModel::closeTab(int row)
{
    if (!valid(row))
        return;
    const QUuid id = m_tabs[static_cast<std::size_t>(row)].data.id;
    beginRemoveRows({}, row, row);
    m_tabs.erase(m_tabs.begin() + row);
    endRemoveRows();
    ensureStore();
    m_store->remove(id);
    emit countChanged();
    if (row < m_current)
        --m_current;
    else if (row == m_current)
        m_current = std::min(row, count() - 1);
    emit currentIndexChanged();
    renumber();
}

bool EditorTabsModel::hasText(int row) const
{
    return valid(row) && !m_tabs[static_cast<std::size_t>(row)].data.text.trimmed().isEmpty();
}

void EditorTabsModel::setCurrentIndex(int index)
{
    if (index == m_current || (index < 0 && count() > 0) || index >= count())
        return;
    m_current = index;
    emit currentIndexChanged();
}

void EditorTabsModel::cycle(int step)
{
    if (count() > 1)
        setCurrentIndex(((m_current + step) % count() + count()) % count());
}

QString EditorTabsModel::idOf(int row) const
{
    return valid(row) ? toId(m_tabs[static_cast<std::size_t>(row)].data.id) : QString();
}

QString EditorTabsModel::titleOf(int row) const
{
    return valid(row) ? m_tabs[static_cast<std::size_t>(row)].data.title : QString();
}

QString EditorTabsModel::connectionIdOf(int row) const
{
    if (!valid(row))
        return {};
    const auto &c = m_tabs[static_cast<std::size_t>(row)].data.connectionId;
    return c ? toId(*c) : QString();
}

QString EditorTabsModel::textOf(int row) const
{
    return valid(row) ? m_tabs[static_cast<std::size_t>(row)].data.text : QString();
}

int EditorTabsModel::cursorOf(int row) const
{
    return valid(row) ? m_tabs[static_cast<std::size_t>(row)].data.cursorPosition : 0;
}

void EditorTabsModel::setText(int row, const QString &text)
{
    if (!valid(row))
        return;
    Tab &tab = m_tabs[static_cast<std::size_t>(row)];
    if (tab.data.text == text)
        return;
    tab.data.text = text;
    tab.stale = true;
    m_splitTimer.start();
    markDirty(row);
}

void EditorTabsModel::setCursor(int row, int position)
{
    if (!valid(row))
        return;
    Tab &tab = m_tabs[static_cast<std::size_t>(row)];
    if (tab.data.cursorPosition == position)
        return;
    tab.data.cursorPosition = position;
    markDirty(row);
}

void EditorTabsModel::setConnection(int row, const QString &connectionId)
{
    if (!valid(row))
        return;
    Tab &tab = m_tabs[static_cast<std::size_t>(row)];
    std::optional<QUuid> connection;
    if (!connectionId.isEmpty()) {
        const QUuid id = QUuid::fromString(connectionId);
        if (!id.isNull())
            connection = id;
    }
    if (tab.data.connectionId == connection)
        return;
    tab.data.connectionId = connection;
    tab.computed = false;
    emit dataChanged(
        index(row), index(row),
        { ConnectionIdRole, ConnectionNameRole, ColorIndexRole, ProductionRole, DefinitionRole });
    emit statementsChanged(row);
    saveNow(row);
}

// ---- statements --------------------------------------------------------------------------------

const ISqlDialect *EditorTabsModel::dialectOf(const Tab &tab, QString *error) const
{
    if (!tab.data.connectionId || !m_connections)
        return nullptr;
    const auto profile = m_connections->model()->profile(*tab.data.connectionId);
    if (!profile)
        return nullptr;
    // shortcut: loads the engine's plugin on the UI thread (a few ms, once per engine), as the
    // schema tree does; move to a worker if plugins get slow to load.
    IDatabaseProvider *provider = m_connections->registry().provider(profile->providerId, error);
    return provider ? &provider->dialect() : nullptr;
}

QString EditorTabsModel::definitionOf(int row) const
{
    if (!valid(row))
        return QStringLiteral("SQL");
    const Tab &tab = m_tabs[static_cast<std::size_t>(row)];
    if (!tab.data.connectionId || !m_connections)
        return QStringLiteral("SQL");
    const auto profile = m_connections->model()->profile(*tab.data.connectionId);
    if (!profile)
        return QStringLiteral("SQL");
    const auto cached = m_definitions.constFind(profile->providerId);
    if (cached != m_definitions.constEnd())
        return cached.value();
    const ISqlDialect *dialect = dialectOf(tab);
    if (!dialect)
        return QStringLiteral("SQL");
    return m_definitions.insert(profile->providerId, dialect->highlightingDefinition()).value();
}

void EditorTabsModel::split(Tab &tab)
{
    tab.statements.clear();
    tab.computed = true;
    tab.stale = false;
    const ISqlDialect *dialect = dialectOf(tab);
    if (!dialect)
        return;
    const QStringView text = tab.data.text;
    qsizetype scanned = 0;
    int line = 1;
    // Positions come in ascending order, so the lines are counted in one pass over the text.
    const auto lineAt = [&](qsizetype pos) {
        pos = std::clamp<qsizetype>(pos, scanned, text.size());
        line += static_cast<int>(text.mid(scanned, pos - scanned).count(QLatin1Char('\n')));
        scanned = pos;
        return line;
    };
    for (const StatementRange &r : dialect->splitStatements(text)) {
        Statement s;
        s.begin = r.begin;
        s.end = r.end;
        s.startLine = lineAt(r.begin);
        s.endLine = lineAt(r.end);
        tab.statements.push_back(s);
    }
}

QVariantMap EditorTabsModel::rangeMap(const Statement &s) const
{
    return { { QStringLiteral("begin"), static_cast<int>(s.begin) },
             { QStringLiteral("end"), static_cast<int>(s.end) },
             { QStringLiteral("startLine"), s.startLine },
             { QStringLiteral("endLine"), s.endLine } };
}

QVariantMap EditorTabsModel::statementAt(int row, int position)
{
    QVariantMap out { { QStringLiteral("index"), -1 }, { QStringLiteral("count"), 0 } };
    if (!valid(row))
        return out;
    Tab &tab = m_tabs[static_cast<std::size_t>(row)];
    if (!tab.computed)
        split(tab);
    const auto &all = tab.statements;
    if (all.empty())
        return out;
    // Inside a statement (its end included); else after the last one that ended before the
    // cursor; else before the first.
    int pick = -1;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (position >= all[i].begin && position <= all[i].end) {
            pick = static_cast<int>(i);
            break;
        }
        if (all[i].end < position)
            pick = static_cast<int>(i);
    }
    if (pick < 0)
        pick = 0;
    out = rangeMap(all[static_cast<std::size_t>(pick)]);
    out.insert(QStringLiteral("index"), pick);
    out.insert(QStringLiteral("count"), static_cast<int>(all.size()));
    return out;
}

QVariantMap EditorTabsModel::run(int row, bool all, int position)
{
    QVariantMap plan { { QStringLiteral("ok"), false },
                       { QStringLiteral("kind"), QStringLiteral("none") },
                       { QStringLiteral("ranges"), QVariantList() } };
    const auto say = [&plan](const QString &message) {
        plan.insert(QStringLiteral("message"), message);
        return plan;
    };
    if (!valid(row))
        return say(tr("No query is open."));
    Tab &tab = m_tabs[static_cast<std::size_t>(row)];
    plan.insert(QStringLiteral("tabId"), toId(tab.data.id));
    if (!tab.data.connectionId)
        return say(tr("Choose a connection first."));
    plan.insert(QStringLiteral("connectionId"), toId(*tab.data.connectionId));
    if (!dialectOf(tab))
        return say(tr("The database engine could not be loaded."));

    split(tab);
    emit statementsChanged(row);
    if (tab.statements.empty())
        return say(tr("Nothing to run."));

    QVariantList ranges;
    QString message;
    if (all) {
        for (const Statement &s : tab.statements)
            ranges.append(rangeMap(s));
        const int n = static_cast<int>(tab.statements.size());
        message = n == 1 ? tr("1 statement") : tr("%1 statements").arg(n);
        plan.insert(QStringLiteral("kind"), QStringLiteral("all"));
        plan.insert(QStringLiteral("count"), n);
    } else {
        const QVariantMap one = statementAt(row, position);
        ranges.append(one);
        const int startLine = one.value(QStringLiteral("startLine")).toInt();
        const int endLine = one.value(QStringLiteral("endLine")).toInt();
        const QString head = tr("Statement %1 of %2")
                                 .arg(one.value(QStringLiteral("index")).toInt() + 1)
                                 .arg(one.value(QStringLiteral("count")).toInt());
        message = startLine == endLine
            ? tr("%1, line %2").arg(head).arg(startLine)
            : tr("%1, lines %2–%3").arg(head).arg(startLine).arg(endLine);
        plan.insert(QStringLiteral("kind"), QStringLiteral("statement"));
        plan.insert(QStringLiteral("index"), one.value(QStringLiteral("index")));
        plan.insert(QStringLiteral("count"), one.value(QStringLiteral("count")));
    }
    plan.insert(QStringLiteral("ok"), true);
    plan.insert(QStringLiteral("ranges"), ranges);
    plan.insert(QStringLiteral("message"), message);
    emit runRequested(plan);
    return plan;
}

} // namespace quarry::ui
