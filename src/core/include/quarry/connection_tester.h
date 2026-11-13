#pragma once

#include <quarry/connection_profile.h>
#include <quarry/provider_api.h>
#include <quarry/result.h>
// Declares the Q_DECLARE_METATYPE for quarry::Error and ServerInfo used by the signals.
#include <quarry/session.h>

#include <QObject>
#include <QTimer>

namespace quarry {

class ProviderRegistry;

// "Test connection" button: opens a throw-away Session and closes it again. The connection
// work runs on the Session thread. One test at a time.
class ConnectionTester : public QObject
{
    Q_OBJECT

public:
    explicit ConnectionTester(ProviderRegistry &registry, QObject *parent = nullptr);

    // Starts a test; a running one is dropped silently (no signal for it). Loads the provider
    // plugin on the calling thread (QPluginLoader), which is acceptable for a one-off user
    // action. Result: succeeded | failed (kind Timeout after 15 s, NotFound for an unknown
    // provider), always delivered from the event loop, never from inside test().
    void test(const ConnectionProfile &profile, const Secrets &secrets);
    // Aborts the running test -> cancelled(). Does nothing if idle.
    void cancel();

    // Test seam: shorten the 15 s timeout.
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }

signals:
    void succeeded(quarry::ServerInfo info);
    void failed(quarry::Error error);
    void cancelled();

private:
    void finishWithError(Error error);
    void reset(); // drops the session and stops the timer

    ProviderRegistry &m_registry;
    Session *m_session = nullptr; // child of this
    QTimer m_timer;
    int m_timeoutMs = 15000;
    quint64 m_generation = 0; // identifies the current test for deferred signals
    bool m_active = false;
};

} // namespace quarry
