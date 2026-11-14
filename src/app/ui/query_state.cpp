#include "query_state.h"

namespace quarry::ui {

namespace {

constexpr int kTickMs = 1000;

} // namespace

QueryState::QueryState(QObject *parent)
    : QObject(parent)
    , m_messages(this)
{
    m_tick.setInterval(kTickMs);
    connect(&m_tick, &QTimer::timeout, this, &QueryState::changed);
}

QString QueryState::state() const
{
    switch (m_state) {
    case State::Idle:
        return QStringLiteral("idle");
    case State::Running:
        return QStringLiteral("running");
    case State::Done:
        return QStringLiteral("done");
    case State::Cancelled:
        return QStringLiteral("cancelled");
    case State::Failed:
        return QStringLiteral("failed");
    }
    return {};
}

int QueryState::elapsedSeconds() const
{
    return m_clock.isValid() ? static_cast<int>(m_clock.elapsed() / 1000) : 0;
}

QString QueryState::elapsedText() const
{
    const int s = elapsedSeconds();
    return QStringLiteral("%1:%2")
        .arg(s / 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString QueryState::resultText() const
{
    if (!m_columns.isEmpty()) {
        if (!m_atEnd)
            return tr("%1+ rows").arg(m_rows);
        return m_rows == 1 ? tr("1 row") : tr("%1 rows").arg(m_rows);
    }
    if (m_affected >= 0)
        return m_affected == 1 ? tr("1 row affected") : tr("%1 rows affected").arg(m_affected);
    return tr("Done");
}

QString QueryState::statusText() const
{
    switch (m_state) {
    case State::Idle:
        return {};
    case State::Running:
        return tr("Running… %1").arg(elapsedText());
    case State::Done:
        return tr("%1 · %2 ms").arg(resultText()).arg(m_elapsedMs);
    case State::Cancelled:
        return tr("Query cancelled");
    case State::Failed:
        return tr("Query failed");
    }
    return {};
}

void QueryState::begin()
{
    m_messages.clear();
    m_hasSummary = false;
    m_rows = 0;
    m_affected = -1;
    m_elapsedMs = 0;
    m_columns.clear();
    m_atEnd = true;
    m_state = State::Running;
    m_clock.start();
    m_tick.start();
    setError(-1, 0, 0, 0);
    emit changed();
}

void QueryState::setSummary(qint64 rows, qint64 affected, qint64 elapsedMs,
                            const QStringList &columns, bool atEnd)
{
    m_hasSummary = true;
    m_rows = rows;
    m_affected = affected;
    m_elapsedMs = elapsedMs;
    m_columns = columns;
    m_atEnd = atEnd;
    emit changed();
}

void QueryState::finish(State state)
{
    m_tick.stop();
    m_state = state;
    emit changed();
}

void QueryState::reset()
{
    m_tick.stop();
    m_messages.clear();
    m_hasSummary = false;
    m_columns.clear();
    m_state = State::Idle;
    setError(-1, 0, 0, 0);
    emit changed();
}

void QueryState::setError(int offset, int length, int line, int column)
{
    if (offset == m_errorOffset && length == m_errorLength && line == m_errorLine
        && column == m_errorColumn)
        return;
    m_errorOffset = offset;
    m_errorLength = length;
    m_errorLine = line;
    m_errorColumn = column;
    emit errorChanged();
}

void QueryState::clearError()
{
    setError(-1, 0, 0, 0);
}

} // namespace quarry::ui
