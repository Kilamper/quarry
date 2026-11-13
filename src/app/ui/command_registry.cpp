#include "command_registry.h"

#include <QLoggingCategory>
#include <QSettings>

#include <algorithm>

namespace quarry::ui {

namespace {

Q_LOGGING_CATEGORY(lcCommands, "quarry.commands")

constexpr QLatin1StringView kKeyRecents { "ui/recentCommands" };

constexpr int kPrefix = 3000;
constexpr int kWordStart = 2000;
constexpr int kSubsequence = 1000;

bool isSubsequence(const QString &needle, const QString &hay)
{
    qsizetype pos = 0;
    for (const QChar c : needle) {
        if (c.isSpace())
            continue;
        pos = hay.indexOf(c, pos);
        if (pos < 0)
            return false;
        ++pos;
    }
    return true;
}

} // namespace

CommandRegistry::CommandRegistry(QObject *parent)
    : QAbstractListModel(parent)
    , m_recents(QSettings().value(kKeyRecents).toStringList())
{
    if (m_recents.size() > kMaxRecents)
        m_recents.resize(kMaxRecents);
}

QString CommandRegistry::displayShortcut(const QString &portable)
{
    return QKeySequence(portable, QKeySequence::PortableText).toString(QKeySequence::NativeText);
}

int CommandRegistry::matchScore(const QString &query, const QString &text)
{
    const QString q = query.trimmed().toCaseFolded();
    const QString t = text.toCaseFolded();
    if (q.isEmpty() || t.isEmpty())
        return 0;
    // Shorter texts win ties: the query covers more of them.
    const int tie = 99 - static_cast<int>(std::min<qsizetype>(t.size(), 99));
    if (t.startsWith(q))
        return kPrefix + tie;
    for (qsizetype i = t.indexOf(q); i > 0; i = t.indexOf(q, i + 1))
        if (!t.at(i - 1).isLetterOrNumber())
            return kWordStart + tie;
    return isSubsequence(q, t) ? kSubsequence + tie : 0;
}

bool CommandRegistry::registerCommand(Command *command)
{
    if (command == nullptr || command->commandId().isEmpty()) {
        qCWarning(lcCommands) << "Rejected command without an id";
        return false;
    }
    if (find(command->commandId()) != nullptr) {
        qCWarning(lcCommands) << "Rejected duplicate command id" << command->commandId();
        return false;
    }
    m_all.append(command);
    connect(command, &Command::changed, this, [this, command] {
        const int row = static_cast<int>(m_rows.indexOf(command));
        if (row >= 0)
            emit dataChanged(index(row), index(row));
    });
    connect(command, &Command::aboutToBeDestroyed, this, [this](Command *gone) {
        m_all.removeAll(gone);
        rebuild();
        emit commandsChanged();
    });
    rebuild();
    emit commandsChanged();
    return true;
}

bool CommandRegistry::unregisterCommand(const QString &id)
{
    Command *c = find(id);
    if (c == nullptr)
        return false;
    c->disconnect(this);
    m_all.removeAll(c);
    rebuild();
    emit commandsChanged();
    return true;
}

Command *CommandRegistry::find(const QString &id) const
{
    for (Command *c : m_all)
        if (c->commandId() == id)
            return c;
    return nullptr;
}

bool CommandRegistry::trigger(const QString &id)
{
    Command *c = find(id);
    if (c == nullptr || !c->isEnabled())
        return false;
    if (c->trackRecent()) {
        m_recents.removeAll(id);
        m_recents.prepend(id);
        if (m_recents.size() > kMaxRecents)
            m_recents.resize(kMaxRecents);
        QSettings().setValue(kKeyRecents, m_recents);
        rebuild(); // recency changes the order
    }
    c->trigger();
    return true;
}

void CommandRegistry::setFilter(const QString &filter)
{
    if (filter == m_filter)
        return;
    m_filter = filter;
    rebuild();
    emit filterChanged();
}

QVariantList CommandRegistry::commandList() const
{
    QVariantList out;
    for (Command *c : m_all)
        out << QVariant::fromValue(c);
    return out;
}

int CommandRegistry::rank(const Command *c) const
{
    const qsizetype i = m_recents.indexOf(c->commandId());
    return i < 0 ? kMaxRecents : static_cast<int>(i);
}

void CommandRegistry::rebuild()
{
    struct Hit
    {
        Command *command;
        int score;
        int recent;
    };
    const bool filtering = !m_filter.trimmed().isEmpty();
    QList<Hit> hits;
    for (Command *c : m_all) {
        int score = 1;
        if (filtering) {
            score = std::max(
                { matchScore(m_filter, c->title()), matchScore(m_filter, c->category()) - 500,
                  matchScore(m_filter, c->category() + QLatin1Char(' ') + c->title()) - 1000 });
        }
        if (score > 0)
            hits.append({ c, score, rank(c) });
    }
    // Stable: ties keep registration order. Without a query every score is equal, so recents
    // come first.
    std::stable_sort(hits.begin(), hits.end(), [filtering](const Hit &a, const Hit &b) {
        if (filtering && a.score != b.score)
            return a.score > b.score;
        return a.recent < b.recent;
    });

    // Remove then insert, not a model reset: QML ListView cancels stale delegate requests after a
    // reset ("DelegateModel::cancel: index out range").
    if (!m_rows.isEmpty()) {
        beginRemoveRows({}, 0, static_cast<int>(m_rows.size()) - 1);
        m_rows.clear();
        endRemoveRows();
    }
    if (!hits.isEmpty()) {
        beginInsertRows({}, 0, static_cast<int>(hits.size()) - 1);
        for (const Hit &h : hits)
            m_rows.append(h.command);
        endInsertRows();
    }
}

int CommandRegistry::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant CommandRegistry::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const Command *c = m_rows.at(index.row());
    switch (role) {
    case IdRole:
        return c->commandId();
    case Qt::DisplayRole:
    case TitleRole:
        return c->title();
    case CategoryRole:
        return c->category();
    case ShortcutRole:
        return c->shortcutText();
    case EnabledRole:
        return c->isEnabled();
    case CheckableRole:
        return c->isCheckable();
    case CheckedRole:
        return c->isChecked();
    default:
        return {};
    }
}

QHash<int, QByteArray> CommandRegistry::roleNames() const
{
    return { { IdRole, "commandId" },           { TitleRole, "title" },
             { CategoryRole, "category" },      { ShortcutRole, "shortcutText" },
             { EnabledRole, "commandEnabled" }, { CheckableRole, "checkable" },
             { CheckedRole, "checked" } };
}

} // namespace quarry::ui
