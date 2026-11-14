#pragma once

#include "query_messages_model.h"

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

namespace quarry::ui {

// What the UI shows about the last run of one editor tab (UX.md section 3, "Ejecucion"): the
// status bar line, the summary of the Results tab, the Messages and the error to underline.
// QueryController owns it and is the only one that changes it; QML only reads.
//
// shortcut: no rows are kept, only counts and column names; the H1.7 grid takes the batches.
class QueryState : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS

    // "idle" | "running" | "done" | "cancelled" | "failed"
    Q_PROPERTY(QString state READ state NOTIFY changed FINAL)
    Q_PROPERTY(bool running READ running NOTIFY changed FINAL)
    // "Running… 00:03", "120 rows · 15 ms", "3 rows affected · 4 ms", "Query cancelled"...
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed FINAL)
    Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY changed FINAL)
    Q_PROPERTY(bool hasSummary READ hasSummary NOTIFY changed FINAL)
    Q_PROPERTY(qint64 rows READ rows NOTIFY changed FINAL)
    Q_PROPERTY(qint64 affectedRows READ affectedRows NOTIFY changed FINAL) // -1 = none
    Q_PROPERTY(qint64 elapsedMs READ elapsedMs NOTIFY changed FINAL)
    Q_PROPERTY(QStringList columns READ columns NOTIFY changed FINAL)
    Q_PROPERTY(bool atEnd READ atEnd NOTIFY changed FINAL)
    Q_PROPERTY(quarry::ui::QueryMessagesModel *messages READ messages CONSTANT FINAL)
    // The error to underline: offset and length in the tab's text, 1-based line and column.
    // -1 = none.
    Q_PROPERTY(int errorOffset READ errorOffset NOTIFY errorChanged FINAL)
    Q_PROPERTY(int errorLength READ errorLength NOTIFY errorChanged FINAL)
    Q_PROPERTY(int errorLine READ errorLine NOTIFY errorChanged FINAL)
    Q_PROPERTY(int errorColumn READ errorColumn NOTIFY errorChanged FINAL)

public:
    enum class State { Idle, Running, Done, Cancelled, Failed };

    explicit QueryState(QObject *parent = nullptr);

    QString state() const;
    bool running() const { return m_state == State::Running; }
    QString statusText() const;
    QString elapsedText() const;
    bool hasSummary() const { return m_hasSummary; }
    qint64 rows() const { return m_rows; }
    qint64 affectedRows() const { return m_affected; }
    qint64 elapsedMs() const { return m_elapsedMs; }
    QStringList columns() const { return m_columns; }
    bool atEnd() const { return m_atEnd; }
    QueryMessagesModel *messages() { return &m_messages; }
    int errorOffset() const { return m_errorOffset; }
    int errorLength() const { return m_errorLength; }
    int errorLine() const { return m_errorLine; }
    int errorColumn() const { return m_errorColumn; }

    // Seconds since begin() (whole seconds, as the status bar shows them).
    int elapsedSeconds() const;
    // "120 rows", "3 rows affected" or "Done" for the summary; no time.
    QString resultText() const;

    // Controller side.
    void begin(); // running, with the previous summary, messages and error cleared
    void setSummary(qint64 rows, qint64 affected, qint64 elapsedMs, const QStringList &columns,
                    bool atEnd);
    void finish(State state);
    void reset(); // back to idle (the connection of the tab changed)
    void setError(int offset, int length, int line, int column);
    Q_INVOKABLE void clearError();

signals:
    void changed();
    void errorChanged();

private:
    State m_state = State::Idle;
    bool m_hasSummary = false;
    qint64 m_rows = 0;
    qint64 m_affected = -1;
    qint64 m_elapsedMs = 0;
    QStringList m_columns;
    bool m_atEnd = true;
    int m_errorOffset = -1;
    int m_errorLength = 0;
    int m_errorLine = 0;
    int m_errorColumn = 0;
    QElapsedTimer m_clock;
    QTimer m_tick;
    QueryMessagesModel m_messages;
};

} // namespace quarry::ui
