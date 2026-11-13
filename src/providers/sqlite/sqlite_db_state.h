#pragma once

#include <atomic>
#include <mutex>
#include <sqlite3.h>

namespace quarry::sqlite {

// Handle shared by a connection and its streams. Everything is single-threaded (the session
// thread) except requestCancel(), which may run anywhere.
struct DbState
{
    sqlite3 *db = nullptr; // written under `mutex` (open/close)
    std::mutex mutex; // guards db lifetime against requestCancel(), and the two flags below
    bool inCall = false; // a provider call is in flight
    bool interruptIssued = false; // sqlite3_interrupt() was called during the current call
    // Our own flag: SQLite clears its interrupt flag when a statement starts with no other
    // active one, so a cancel landing between steps would otherwise be lost.
    std::atomic<bool> cancel { false };

    DbState() = default;
    DbState(const DbState &) = delete;
    DbState &operator=(const DbState &) = delete;
    ~DbState()
    {
        if (db)
            sqlite3_close_v2(db);
    }

    void requestCancel()
    {
        std::lock_guard lock(mutex);
        cancel.store(true, std::memory_order_release);
        // Interrupting an idle connection would leave a stale flag if a statement is still
        // active (open stream between fetches), so only interrupt while a call is running.
        if (inCall && db) {
            interruptIssued = true;
            sqlite3_interrupt(db);
        }
    }

    bool wasInterrupted()
    {
        std::lock_guard lock(mutex);
        return interruptIssued;
    }
};

// Marks one provider call (open/children/describe/execute/fetch) and discards any cancel
// request that arrived before it started (cancellation contract).
class CallScope
{
public:
    explicit CallScope(DbState &state)
        : m_state(state)
    {
        std::lock_guard lock(m_state.mutex);
        m_state.cancel.store(false, std::memory_order_release);
        m_state.interruptIssued = false;
        m_state.inCall = true;
    }
    CallScope(const CallScope &) = delete;
    CallScope &operator=(const CallScope &) = delete;
    ~CallScope()
    {
        std::lock_guard lock(m_state.mutex);
        m_state.inCall = false;
    }

private:
    DbState &m_state;
};

} // namespace quarry::sqlite
