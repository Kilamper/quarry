#include "query_controller.h"

#include "connection_controller.h"
#include "editor_tabs_model.h"

#include <quarry/sql_safety.h>

#include <algorithm>

namespace quarry::ui {

namespace {

constexpr int kPreviewChars = 160;

QString toId(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

QString riskText(SqlRisk risk)
{
    switch (risk) {
    case SqlRisk::UpdateWithoutWhere:
        return QueryController::tr("UPDATE without WHERE");
    case SqlRisk::DeleteWithoutWhere:
        return QueryController::tr("DELETE without WHERE");
    case SqlRisk::Drop:
        return QueryController::tr("DROP on a production connection");
    case SqlRisk::Truncate:
        return QueryController::tr("TRUNCATE on a production connection");
    case SqlRisk::None:
        break;
    }
    return {};
}

QString kindText(ErrorKind kind)
{
    switch (kind) {
    case ErrorKind::Connection:
        return QueryController::tr("Connection error");
    case ErrorKind::Authentication:
        return QueryController::tr("Authentication error");
    case ErrorKind::Timeout:
        return QueryController::tr("Timeout");
    case ErrorKind::Cancelled:
        return QueryController::tr("Cancelled");
    case ErrorKind::Syntax:
        return QueryController::tr("Syntax error");
    case ErrorKind::Permission:
        return QueryController::tr("Permission denied");
    case ErrorKind::Constraint:
        return QueryController::tr("Constraint violation");
    case ErrorKind::NotFound:
        return QueryController::tr("Not found");
    case ErrorKind::Unsupported:
        return QueryController::tr("Not supported");
    case ErrorKind::Driver:
        return QueryController::tr("Database error");
    case ErrorKind::Internal:
        break;
    }
    return QueryController::tr("Internal error");
}

QString preview(const QString &sql)
{
    QString one = sql.simplified();
    if (one.size() > kPreviewChars)
        one = one.left(kPreviewChars) + QStringLiteral("…");
    return one;
}

bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

Error gone()
{
    return Error {
        ErrorKind::NotFound, QueryController::tr("This connection no longer exists."), {}, {}, {}
    };
}

} // namespace

QueryController::QueryController(QObject *parent)
    : QObject(parent)
    , m_none(new QueryState(this))
    , m_current(m_none)
{ }

QueryController::~QueryController() = default; // the sessions are children: joined here

void QueryController::setTabs(EditorTabsModel *tabs)
{
    if (tabs == m_tabs)
        return;
    if (m_tabs)
        m_tabs->disconnect(this);
    m_tabs = tabs;
    if (m_tabs) {
        connect(m_tabs, &EditorTabsModel::runRequested, this, &QueryController::onRunRequested);
        connect(m_tabs, &QAbstractItemModel::rowsRemoved, this, &QueryController::sync);
        connect(m_tabs, &QAbstractItemModel::modelReset, this, &QueryController::sync);
        // Connection changes (also when the connection is deleted) arrive as dataChanged.
        connect(m_tabs, &QAbstractItemModel::dataChanged, this, &QueryController::sync);
        connect(m_tabs, &EditorTabsModel::currentIndexChanged, this, &QueryController::sync);
        connect(m_tabs, &EditorTabsModel::countChanged, this, &QueryController::sync);
    }
    sync();
    emit tabsChanged();
}

ConnectionController *QueryController::connections() const
{
    return m_tabs ? m_tabs->connections() : nullptr;
}

void QueryController::bindConnections()
{
    ConnectionController *c = connections();
    if (c == m_connections)
        return;
    if (m_connections)
        m_connections->disconnect(this);
    m_connections = c;
    if (c) {
        connect(c, &ConnectionController::passwordAvailable, this,
                &QueryController::onPasswordAvailable);
        connect(c, &ConnectionController::passwordUnavailable, this,
                &QueryController::onPasswordUnavailable);
    }
}

int QueryController::sessionCount() const
{
    return static_cast<int>(std::ranges::count_if(
        m_tabStates, [](const auto &entry) { return entry.second->session != nullptr; }));
}

// ---- tabs --------------------------------------------------------------------------------------

int QueryController::rowOf(const QString &tabId) const
{
    if (!m_tabs || tabId.isEmpty())
        return -1;
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->idOf(i) == tabId)
            return i;
    return -1;
}

QString QueryController::title(const QString &tabId) const
{
    const int row = rowOf(tabId);
    return row >= 0 ? m_tabs->titleOf(row) : QString();
}

QueryController::Tab *QueryController::find(const QString &tabId)
{
    const auto it = m_tabStates.find(tabId);
    return it == m_tabStates.end() ? nullptr : it->second.get();
}

QueryController::Tab &QueryController::tabFor(const QString &tabId)
{
    auto &slot = m_tabStates[tabId];
    if (!slot) {
        slot = std::make_unique<Tab>();
        slot->id = tabId;
        slot->state = new QueryState(this);
    }
    return *slot;
}

QueryState *QueryController::stateOf(const QString &tabId)
{
    return rowOf(tabId) < 0 ? nullptr : tabFor(tabId).state;
}

bool QueryController::hasSession(const QString &tabId) const
{
    const auto it = m_tabStates.find(tabId);
    return it != m_tabStates.end() && it->second->session != nullptr;
}

QStringList QueryController::tabIds() const
{
    QStringList ids;
    for (const auto &entry : m_tabStates)
        ids.append(entry.first);
    return ids;
}

void QueryController::updateCurrent()
{
    QueryState *s = m_none;
    if (m_tabs) {
        const QString id = m_tabs->idOf(m_tabs->currentIndex());
        if (!id.isEmpty())
            s = tabFor(id).state;
    }
    if (s == m_current)
        return;
    m_current = s;
    emit currentChanged();
}

// A tab that is gone, or whose connection changed, loses its session, its plan and its results.
void QueryController::sync()
{
    if (!m_tabs)
        return;
    const int before = sessionCount();
    for (const QString &id : tabIds()) {
        Tab *t = find(id);
        if (t == nullptr)
            continue;
        const int row = rowOf(id);
        if (row < 0) {
            dropPlan(*t);
            closeSession(*t);
            if (t->state == m_current) {
                m_current = m_none;
                emit currentChanged();
            }
            t->state->deleteLater();
            m_tabStates.erase(id);
        } else if (!t->connectionId.isEmpty() && m_tabs->connectionIdOf(row) != t->connectionId) {
            dropPlan(*t);
            closeSession(*t);
            t->connectionId.clear();
            t->state->reset();
        }
    }
    updateCurrent();
    if (sessionCount() != before)
        emit sessionsChanged();
}

void QueryController::dropPlan(Tab &t)
{
    t.plan.reset();
    t.pending.reset();
    t.stage = Stage::None;
    t.cancelRequested = false;
    t.ops.clear();
}

void QueryController::closeSession(Tab &t)
{
    t.opening = false;
    t.ops.clear();
    if (t.session == nullptr)
        return;
    Session *s = t.session;
    t.session = nullptr;
    s->disconnect(this);
    // The session ends on its own thread; it is deleted once it says so.
    connect(s, &Session::closed, s, &QObject::deleteLater);
    s->close();
}

// ---- running -----------------------------------------------------------------------------------

void QueryController::onRunRequested(const QVariantMap &request)
{
    bindConnections();
    const QString tabId = request.value(QStringLiteral("tabId")).toString();
    const int row = rowOf(tabId);
    ConnectionController *conns = connections();
    if (row < 0 || conns == nullptr)
        return;
    Tab &t = tabFor(tabId);

    const bool all = request.value(QStringLiteral("kind")).toString() == QLatin1String("all");
    Plan plan;
    plan.connectionId = request.value(QStringLiteral("connectionId")).toString();
    plan.text = m_tabs->textOf(row);
    const QVariantList ranges = request.value(QStringLiteral("ranges")).toList();
    plan.total
        = all ? static_cast<int>(ranges.size()) : request.value(QStringLiteral("count")).toInt();
    const int index = request.value(QStringLiteral("index")).toInt();
    int i = 0;
    for (const QVariant &v : ranges) {
        const QVariantMap r = v.toMap();
        Range range;
        range.begin = r.value(QStringLiteral("begin")).toInt();
        const qsizetype end = r.value(QStringLiteral("end")).toInt();
        range.sql = plan.text.mid(range.begin, end - range.begin);
        range.startLine = r.value(QStringLiteral("startLine")).toInt();
        range.number = all ? i + 1 : index + 1;
        plan.ranges.push_back(std::move(range));
        ++i;
    }

    const auto profile = conns->model()->profile(QUuid::fromString(plan.connectionId));
    if (!profile) {
        t.plan = std::move(plan);
        t.next = 0;
        t.state->begin();
        failPlan(t, gone());
        return;
    }
    plan.production = profile->production;
    plan.connectionName = profile->name;

    // One confirmation for the whole plan, before anything runs.
    QVariantList items;
    for (const Range &r : std::as_const(plan.ranges)) {
        const SqlRisk risk = SqlSafety::check(r.sql, plan.production);
        if (risk == SqlRisk::None)
            continue;
        items.append(QVariantMap {
            { QStringLiteral("label"),
              tr("Statement %1 of %2, line %3").arg(r.number).arg(plan.total).arg(r.startLine) },
            { QStringLiteral("risk"), riskText(risk) },
            { QStringLiteral("sql"), preview(r.sql) } });
    }
    if (items.isEmpty()) {
        t.pending.reset();
        start(t, std::move(plan));
        return;
    }
    const QString name = plan.connectionName;
    const bool production = plan.production;
    t.pending = std::move(plan);
    emit confirmationRequested({ { QStringLiteral("tabId"), tabId },
                                 { QStringLiteral("connectionName"), name },
                                 { QStringLiteral("production"), production },
                                 { QStringLiteral("items"), items } });
}

void QueryController::confirm(const QString &tabId)
{
    Tab *t = find(tabId);
    if (t == nullptr || !t->pending)
        return;
    Plan plan = std::move(*t->pending);
    t->pending.reset();
    start(*t, std::move(plan));
}

void QueryController::decline(const QString &tabId)
{
    Tab *t = find(tabId);
    if (t == nullptr || !t->pending)
        return;
    t->pending.reset();
    announce(tabId, tr("Cancelled. Nothing was run."), false);
}

void QueryController::start(Tab &t, Plan plan)
{
    // A plan that was running is replaced: Session::execute() cancels its statement, and the
    // terminal signal of that statement is skipped (see Tab::ops).
    t.connectionId = plan.connectionId;
    t.plan = std::move(plan);
    t.next = 0;
    t.planId = ++m_nextPlan;
    t.cancelRequested = false;
    t.stage = Stage::None;
    t.columns.clear();
    t.elapsedTotal = 0;
    t.state->begin();
    if (t.session != nullptr)
        runNext(t);
    else
        openSession(t);
}

void QueryController::openSession(Tab &t)
{
    ConnectionController *conns = connections();
    if (conns == nullptr)
        return;
    const auto profile = conns->model()->profile(QUuid::fromString(t.connectionId));
    if (!profile) {
        failPlan(t, gone());
        return;
    }
    switch (profile->passwordMode) {
    case PasswordMode::None:
        proceed(t, {});
        break;
    case PasswordMode::Saved:
        t.stage = Stage::ReadingPassword;
        conns->readPassword(profile->id);
        break;
    case PasswordMode::AskEveryTime:
        if (const auto pw = conns->sessionPassword(profile->id)) {
            proceed(t, *pw);
            break;
        }
        t.stage = Stage::AwaitingPassword;
        emit passwordRequested(toId(profile->id), profile->name,
                               m_authFailed.contains(toId(profile->id)));
        break;
    }
}

void QueryController::proceed(Tab &t, const QString &password)
{
    t.stage = Stage::None;
    ConnectionController *conns = connections();
    if (conns == nullptr)
        return;
    const auto profile = conns->model()->profile(QUuid::fromString(t.connectionId));
    if (!profile) {
        failPlan(t, gone());
        return;
    }
    // shortcut: loads the provider plugin on the UI thread (a few ms, once per engine), like the
    // schema tree; move it to the session thread when plugins grow heavy.
    auto connection = conns->registry().createConnection(*profile, Secrets { password });
    if (!connection) {
        failPlan(t, connection.error());
        return;
    }
    const QString id = t.id;
    auto *s = new Session(std::move(connection).value(), this);
    t.session = s;
    t.opening = true;
    connect(s, &Session::opened, this, [this, id] { onOpened(id); });
    connect(s, &Session::columnsReady, this,
            [this, id](const std::vector<ColumnInfo> &columns) { onColumns(id, columns); });
    connect(s, &Session::finished, this,
            [this, id](const ExecutionStats &stats) { onFinished(id, stats); });
    connect(s, &Session::failed, this, [this, id](const Error &e) { onFailed(id, e); });
    connect(s, &Session::cancelled, this, [this, id] { onCancelled(id); });
    // batchReady is deliberately not connected: nothing keeps the rows until the H1.7 grid.
    s->open();
    emit sessionsChanged();
    runNext(t);
}

void QueryController::runNext(Tab &t)
{
    if (!t.plan || t.session == nullptr)
        return;
    if (t.next >= t.plan->ranges.size()) {
        finishDone(t);
        return;
    }
    const Range &r = t.plan->ranges.at(t.next);
    t.columns.clear();
    announce(t.id, tr("Running statement %1 of %2").arg(r.number).arg(t.plan->total), false);
    t.ops.push_back(t.planId);
    Statement statement;
    statement.sql = r.sql;
    statement.fetchSize = kFetchSize;
    t.session->execute(statement);
}

bool QueryController::currentOp(const Tab &t) const
{
    return t.plan && !t.ops.isEmpty() && t.ops.first() == t.planId;
}

// Takes the finished execute off the list; true when it belongs to the plan that is running.
bool QueryController::popOp(Tab &t)
{
    if (t.ops.isEmpty())
        return false;
    const quint64 id = t.ops.takeFirst();
    return t.plan && id == t.planId;
}

void QueryController::onOpened(const QString &tabId)
{
    Tab *t = find(tabId);
    if (t == nullptr)
        return;
    t->opening = false;
    m_authFailed.remove(t->connectionId);
}

void QueryController::onColumns(const QString &tabId, const std::vector<ColumnInfo> &columns)
{
    Tab *t = find(tabId);
    if (t == nullptr || !currentOp(*t))
        return;
    t->columns.clear();
    for (const ColumnInfo &c : columns)
        t->columns.append(c.name);
}

void QueryController::onFinished(const QString &tabId, const ExecutionStats &stats)
{
    Tab *t = find(tabId);
    if (t == nullptr || !popOp(*t) || !t->plan)
        return;
    t->elapsedTotal += stats.elapsedMs;
    t->state->setSummary(stats.rowsFetched, stats.affectedRows.value_or(-1), t->elapsedTotal,
                         t->columns, stats.atEnd);
    const Range &r = t->plan->ranges.at(t->next);
    t->state->messages()->append({ QStringLiteral("statement"),
                                   tr("Statement"),
                                   tr("Statement %1 of %2: %3")
                                       .arg(r.number)
                                       .arg(t->plan->total)
                                       .arg(t->state->resultText()),
                                   {},
                                   0,
                                   0 });
    for (const QString &n : stats.notices)
        t->state->messages()->append({ QStringLiteral("notice"), tr("Notice"), n, {}, 0, 0 });
    if (t->cancelRequested) { // the statement ended before the cancel reached it
        finishCancelled(*t);
        return;
    }
    ++t->next;
    runNext(*t);
}

void QueryController::onFailed(const QString &tabId, const Error &error)
{
    Tab *t = find(tabId);
    if (t == nullptr)
        return;
    if (t->opening) { // open() failed; the statement queued behind it goes with the session
        ConnectionController *conns = connections();
        if (error.kind == ErrorKind::Authentication && conns != nullptr) {
            m_authFailed.insert(t->connectionId);
            // A wrong password must not be tried again silently.
            conns->forgetPassword(QUuid::fromString(t->connectionId));
        }
        closeSession(*t);
        if (t->plan)
            failPlan(*t, error);
        return;
    }
    if (popOp(*t))
        failPlan(*t, error);
}

void QueryController::onCancelled(const QString &tabId)
{
    Tab *t = find(tabId);
    if (t == nullptr)
        return;
    if (t->opening) {
        closeSession(*t);
        if (t->plan)
            finishCancelled(*t);
        return;
    }
    if (popOp(*t))
        finishCancelled(*t);
}

void QueryController::cancel(const QString &tabId)
{
    Tab *t = find(tabId);
    if (t == nullptr || !t->plan)
        return;
    t->cancelRequested = true;
    // Waiting for a password or for the keychain, or nothing sent yet: nothing to interrupt.
    if (t->stage != Stage::None || t->session == nullptr || t->ops.isEmpty()) {
        t->stage = Stage::None;
        finishCancelled(*t);
        return;
    }
    t->session->cancel();
}

void QueryController::cancelCurrent()
{
    if (m_tabs)
        cancel(m_tabs->idOf(m_tabs->currentIndex()));
}

void QueryController::finishDone(Tab &t)
{
    const QueryState &s = *t.state;
    QString text;
    if (!s.columns().isEmpty())
        text = tr("Done: %1 in %2 ms").arg(s.resultText()).arg(s.elapsedMs());
    else if (s.affectedRows() >= 0)
        text = s.resultText();
    else
        text = tr("Done in %1 ms").arg(s.elapsedMs());
    t.plan.reset();
    t.state->finish(QueryState::State::Done);
    announce(t.id, text, false);
}

void QueryController::finishCancelled(Tab &t)
{
    t.plan.reset();
    t.cancelRequested = false;
    t.state->messages()->append(
        { QStringLiteral("cancelled"), tr("Cancelled"), tr("Query cancelled"), {}, 0, 0 });
    t.state->finish(QueryState::State::Cancelled);
    announce(t.id, tr("Query cancelled"), false);
}

void QueryController::failPlan(Tab &t, const Error &error)
{
    int offset = -1;
    int length = 0;
    int line = 0;
    int column = 0;
    const int row = rowOf(t.id);
    const int position = error.position.value_or(-1);
    // The position only means something for the text that was run.
    if (t.plan && position >= 0 && t.next < t.plan->ranges.size() && row >= 0
        && !t.plan->text.isEmpty() && m_tabs->textOf(row) == t.plan->text) {
        const QString &text = t.plan->text;
        const Range &r = t.plan->ranges.at(t.next);
        const qsizetype size = text.size();
        // At the very end (e.g. "unexpected end of input") the last character is underlined.
        offset = static_cast<int>(
            std::min(r.begin + std::min<qsizetype>(position, r.sql.size()), size - 1));
        qsizetype end = offset;
        while (end < size && isWordChar(text.at(end)))
            ++end;
        length = std::max(1, static_cast<int>(end) - offset);
        line = 1 + static_cast<int>(QStringView(text).left(offset).count(QLatin1Char('\n')));
        const qsizetype lastBreak
            = offset > 0 ? text.lastIndexOf(QLatin1Char('\n'), offset - 1) : -1;
        column = static_cast<int>(offset - lastBreak);
    }
    t.plan.reset();
    t.cancelRequested = false;
    t.state->messages()->append({ QStringLiteral("error"), kindText(error.kind), error.message,
                                  error.detail, line, column });
    t.state->setError(offset, length, line, column);
    t.state->finish(QueryState::State::Failed);
    QString text = error.message;
    if (line > 0)
        text += QLatin1Char(' ') + tr("Error at line %1, column %2").arg(line).arg(column);
    announce(t.id, text, true);
    emit planFailed(t.id);
}

void QueryController::onPasswordAvailable(QUuid id, const QString &password)
{
    const QString key = toId(id);
    for (const QString &tabId : tabIds()) {
        Tab *t = find(tabId);
        if (t != nullptr && t->plan && t->stage == Stage::ReadingPassword && t->connectionId == key)
            proceed(*t, password);
    }
}

void QueryController::onPasswordUnavailable(QUuid id, const Error &error)
{
    const QString key = toId(id);
    for (const QString &tabId : tabIds()) {
        Tab *t = find(tabId);
        if (t != nullptr && t->plan && t->stage == Stage::ReadingPassword
            && t->connectionId == key) {
            t->stage = Stage::None;
            failPlan(*t, error);
        }
    }
}

void QueryController::providePassword(const QString &connectionId, const QString &password)
{
    ConnectionController *conns = connections();
    if (conns == nullptr)
        return;
    conns->rememberPassword(QUuid::fromString(connectionId), password);
    for (const QString &tabId : tabIds()) {
        Tab *t = find(tabId);
        if (t != nullptr && t->plan && t->stage == Stage::AwaitingPassword
            && t->connectionId == connectionId)
            proceed(*t, password);
    }
}

void QueryController::cancelPassword(const QString &connectionId)
{
    for (const QString &tabId : tabIds()) {
        Tab *t = find(tabId);
        if (t != nullptr && t->plan && t->stage == Stage::AwaitingPassword
            && t->connectionId == connectionId) {
            t->stage = Stage::None;
            finishCancelled(*t);
        }
    }
}

void QueryController::announce(const QString &tabId, const QString &text, bool assertive)
{
    const bool isCurrent = m_tabs && m_tabs->idOf(m_tabs->currentIndex()) == tabId;
    emit announcement(isCurrent ? text : tr("%1: %2").arg(title(tabId), text), assertive);
}

} // namespace quarry::ui
