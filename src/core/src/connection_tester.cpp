#include <quarry/connection_tester.h>
#include <quarry/provider_registry.h>

namespace quarry {

ConnectionTester::ConnectionTester(ProviderRegistry &registry, QObject *parent)
    : QObject(parent)
    , m_registry(registry)
{
    qRegisterMetaType<quarry::Error>();
    qRegisterMetaType<quarry::ServerInfo>();
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        reset(); // cancels the pending open; the session is destroyed from the event loop
        finishWithError({ ErrorKind::Timeout,
                          tr("The server did not answer in 15 seconds."),
                          tr("The connection test was stopped after %1 ms.").arg(m_timeoutMs),
                          {},
                          {} });
    });
}

void ConnectionTester::reset()
{
    m_timer.stop();
    if (m_session) {
        m_session->disconnect(this);
        m_session->cancel();
        // Closing is not needed: the connection is destroyed with the session.
        m_session->deleteLater();
        m_session = nullptr;
    }
}

void ConnectionTester::finishWithError(Error error)
{
    reset();
    m_active = false;
    ++m_generation;
    emit failed(std::move(error));
}

void ConnectionTester::test(const ConnectionProfile &profile, const Secrets &secrets)
{
    reset();
    m_active = true;
    const quint64 generation = ++m_generation;

    auto failLater = [this, generation](Error error) {
        QTimer::singleShot(0, this, [this, generation, error = std::move(error)]() mutable {
            if (m_active && m_generation == generation)
                finishWithError(std::move(error));
        });
    };

    auto connection = m_registry.createConnection(profile, secrets);
    if (!connection) {
        failLater(std::move(connection).error());
        return;
    }

    m_session = new Session(std::move(connection).value(), this);
    connect(m_session, &Session::opened, this, [this](const ServerInfo &info) {
        Session *session = m_session;
        m_session = nullptr; // detach: it closes itself and is deleted when done
        m_timer.stop();
        session->disconnect(this);
        connect(session, &Session::closed, session, &QObject::deleteLater);
        session->close();
        m_active = false;
        ++m_generation;
        emit succeeded(info);
    });
    connect(m_session, &Session::failed, this,
            [this](const Error &error) { finishWithError(error); });
    connect(m_session, &Session::cancelled, this, [this] {
        // Only reachable if the provider aborted on its own: report it as a failure.
        finishWithError(
            { ErrorKind::Cancelled, tr("The connection test was cancelled."), {}, {}, {} });
    });
    m_timer.start(m_timeoutMs);
    m_session->open();
}

void ConnectionTester::cancel()
{
    if (!m_active)
        return;
    reset();
    m_active = false;
    ++m_generation;
    emit cancelled();
}

} // namespace quarry
