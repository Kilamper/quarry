#include "connection_controller.h"

#include "theme_tokens.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>

#include <algorithm>
#include <utility>

namespace quarry::ui {

Q_LOGGING_CATEGORY(lcConnections, "quarry.app.connections")

namespace {

constexpr int kPasswordModeMax = static_cast<int>(PasswordMode::AskEveryTime);

QString toId(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

QString hintFor(ErrorKind kind)
{
    switch (kind) {
    case ErrorKind::Authentication:
        return ConnectionController::tr("Check the user name and the password.");
    case ErrorKind::Connection:
        return ConnectionController::tr("Check the host, the port and that the server is running.");
    case ErrorKind::Timeout:
        return ConnectionController::tr("The server did not answer in 15 seconds.");
    default:
        return {};
    }
}

} // namespace

ConnectionController::ConnectionController(QObject *parent)
    : QObject(parent)
    , m_model({}, this)
    , m_form(this)
    , m_tester(m_registry, this)
    , m_credentials(this)
    , m_tree(&m_model, &m_registry, this)
{
    m_backend.write
        = [this](QUuid id, QString pw) { m_credentials.writePassword(id, std::move(pw)); };
    m_backend.read = [this](QUuid id) { m_credentials.readPassword(id); };
    m_backend.remove = [this](QUuid id) { m_credentials.deletePassword(id); };
    m_backend.forget = [this](QUuid id) { m_credentials.forgetForSession(id); };

    SchemaCredentials tree;
    tree.read = [this](QUuid id) { m_backend.read(id); };
    tree.session = [this](QUuid id) { return m_credentials.sessionPassword(id); };
    tree.remember
        = [this](QUuid id, QString pw) { m_credentials.rememberForSession(id, std::move(pw)); };
    tree.forget = [this](QUuid id) { m_backend.forget(id); };
    m_tree.setCredentials(std::move(tree));

    connect(&m_credentials, &CredentialStore::passwordRead, this,
            &ConnectionController::onPasswordRead);
    connect(&m_credentials, &CredentialStore::passwordWritten, this,
            &ConnectionController::onPasswordWritten);
    connect(&m_credentials, &CredentialStore::passwordDeleted, this,
            &ConnectionController::onPasswordDeleted);
    connect(&m_credentials, &CredentialStore::failed, this,
            &ConnectionController::onCredentialFailed);

    // A result that arrives after the test was cancelled or reset is stale.
    connect(&m_tester, &ConnectionTester::succeeded, this, [this](const ServerInfo &info) {
        if (m_testState != QLatin1String("testing"))
            return;
        setTest(QStringLiteral("success"),
                tr("Connected to %1 %2").arg(info.product, info.version).trimmed());
    });
    connect(&m_tester, &ConnectionTester::failed, this, [this](const Error &error) {
        if (m_testState != QLatin1String("testing"))
            return;
        setTest(QStringLiteral("failed"), error.message, hintFor(error.kind), error.detail);
    });
    connect(&m_tester, &ConnectionTester::cancelled, this,
            [this] { setTest(QStringLiteral("idle")); });
}

void ConnectionController::setDatabasePath(const QString &path)
{
    if (path == m_databasePath)
        return;
    m_databasePath = path;
    m_model.setDatabasePath(path);
    emit configChanged();
}

void ConnectionController::setProviderDirectory(const QString &dir)
{
    if (dir == m_providerDirectory)
        return;
    m_providerDirectory = dir;
    m_scanned = false;
    emit configChanged();
}

void ConnectionController::ensureRegistry()
{
    if (m_scanned)
        return;
    m_scanned = true;
    // shortcut: synchronous metadata scan on the UI thread (no library is loaded); move it to a
    // worker when engines are numerous.
    m_registry.scan(m_providerDirectory.isEmpty() ? ProviderRegistry::defaultPluginDirectory()
                                                  : m_providerDirectory);
    m_model.setRegistry(&m_registry);
    m_engines.clear();
    for (const auto &e : m_registry.available())
        m_engines.append(QVariantMap { { QStringLiteral("id"), e.id },
                                       { QStringLiteral("name"), e.displayName } });
    qCInfo(lcConnections) << "Providers available:" << m_registry.available().size();
    emit enginesChanged();
}

void ConnectionController::connectStore()
{
    if (m_storeConnected)
        return;
    m_storeConnected = true;
    ConnectionStore *s = m_model.store(); // after the model's own connections
    connect(s, &ConnectionStore::saved, this, &ConnectionController::onStoreSaved);
    connect(s, &ConnectionStore::removed, this, &ConnectionController::onStoreRemoved);
    connect(s, &ConnectionStore::failed, this, &ConnectionController::onStoreFailed);
}

void ConnectionController::start()
{
    if (m_started)
        return;
    m_started = true;
    ensureRegistry();
    connectStore();
    m_model.start();
    emit startedChanged();
}

// ---- dialog support ----------------------------------------------------------------------------

QVariantMap ConnectionController::draftOf(const ConnectionProfile &p) const
{
    return { { QStringLiteral("id"), p.id.isNull() ? QString() : toId(p.id) },
             { QStringLiteral("providerId"), p.providerId },
             { QStringLiteral("name"), p.name },
             { QStringLiteral("colorIndex"), p.colorIndex },
             { QStringLiteral("production"), p.production },
             { QStringLiteral("passwordMode"), static_cast<int>(p.passwordMode) } };
}

QVariantMap ConnectionController::prepare(const QString &id)
{
    ensureRegistry();
    resetTest();
    if (id.isEmpty()) {
        ConnectionProfile p;
        p.passwordMode = PasswordMode::Saved;
        const auto &all = m_registry.available();
        const bool hasSqlite = std::ranges::any_of(
            all, [](const ProviderEntry &e) { return e.id == QLatin1String("sqlite"); });
        if (hasSqlite)
            p.providerId = QStringLiteral("sqlite");
        else if (!all.empty())
            p.providerId = all.front().id;
        if (!p.providerId.isEmpty())
            selectEngine(p.providerId);
        else
            m_form.setFields({});
        return draftOf(p);
    }
    const auto profile = m_model.profile(QUuid::fromString(id));
    if (!profile) {
        m_form.setFields({});
        return {};
    }
    selectEngine(profile->providerId);
    m_form.setValues(profile->values);
    return draftOf(*profile);
}

bool ConnectionController::selectEngine(const QString &providerId)
{
    ensureRegistry();
    resetTest();
    QString error;
    IDatabaseProvider *provider = m_registry.provider(providerId, &error);
    if (provider == nullptr) {
        m_form.setFields({});
        setError(tr("The database engine could not be loaded."), error);
        return false;
    }
    m_form.setFields(provider->connectionFields());
    return true;
}

ConnectionProfile ConnectionController::profileFrom(const QVariantMap &draft) const
{
    ConnectionProfile p;
    p.id = QUuid::fromString(draft.value(QStringLiteral("id")).toString());
    p.name = draft.value(QStringLiteral("name")).toString().trimmed();
    p.providerId = draft.value(QStringLiteral("providerId")).toString();
    p.values = m_form.values();
    p.colorIndex = draft.value(QStringLiteral("colorIndex"), -1).toInt();
    if (p.colorIndex < -1 || p.colorIndex >= kConnectionColorCount)
        p.colorIndex = -1;
    p.production = draft.value(QStringLiteral("production")).toBool();
    // Without a Secret field there is nothing to ask for or to keep.
    const int mode
        = std::clamp(draft.value(QStringLiteral("passwordMode"), 1).toInt(), 0, kPasswordModeMax);
    p.passwordMode = !m_form.hasSecret()               ? PasswordMode::None
        : mode == static_cast<int>(PasswordMode::None) ? PasswordMode::AskEveryTime
                                                       : static_cast<PasswordMode>(mode);
    return p;
}

bool ConnectionController::save(const QVariantMap &draft)
{
    if (m_pending)
        return false;
    ConnectionProfile profile = profileFrom(draft);
    const bool formOk = m_form.isValid();
    if (profile.name.isEmpty() || !formOk || profile.providerId.isEmpty())
        return false;

    Pending pending;
    pending.kind = Kind::Save;
    if (profile.id.isNull()) {
        profile.id = QUuid::createUuid(); // known now: the password is written under it first
    } else if (const auto old = m_model.profile(profile.id)) {
        pending.previousMode = old->passwordMode;
    }
    const bool touched = draft.value(QStringLiteral("passwordTouched")).toBool();
    pending.password = draft.value(QStringLiteral("password")).toString();
    pending.profile = profile;
    const bool writePassword
        = profile.passwordMode == PasswordMode::Saved && touched && !pending.password.isEmpty();
    pending.stage = writePassword ? Stage::WritingPassword : Stage::Storing;
    m_pending = std::move(pending);
    connectStore();
    emit savingChanged();

    if (writePassword) {
        // Keychain first: if it fails nothing is stored and the user can switch to "Ask every
        // time".
        m_backend.write(profile.id, std::exchange(m_pending->password, QString()));
    } else {
        storeNow(m_form.secretKeys());
    }
    return true;
}

void ConnectionController::storeNow(const QStringList &secretKeys)
{
    if (!m_pending)
        return;
    m_pending->stage = Stage::Storing;
    m_lastSecretKeys = secretKeys;
    m_model.store()->save(m_pending->profile, secretKeys);
}

void ConnectionController::endPending()
{
    m_pending.reset();
    emit savingChanged();
}

void ConnectionController::fail(const QString &message, const QString &detail, bool keychain)
{
    if (!m_pending)
        return;
    const Kind kind = m_pending->kind;
    endPending();
    if (kind == Kind::Save)
        emit saveFailed(message, detail, keychain);
    else
        setError(message, detail);
}

void ConnectionController::onPasswordWritten(QUuid id)
{
    if (m_pending && m_pending->stage == Stage::WritingPassword && m_pending->profile.id == id)
        storeNow(m_form.secretKeys());
}

void ConnectionController::onStoreSaved(const ConnectionProfile &profile)
{
    if (!m_pending || m_pending->stage != Stage::Storing || m_pending->profile.id != profile.id)
        return;
    const Pending done = *m_pending;
    endPending();
    if (done.previousMode == PasswordMode::Saved && profile.passwordMode != PasswordMode::Saved) {
        m_backend.remove(profile.id); // the keychain entry no longer belongs to this mode
    }
    if (done.kind == Kind::Duplicate)
        emit connectionDuplicated(toId(profile.id));
    else
        emit connectionSaved(toId(profile.id));
}

void ConnectionController::onStoreFailed(const Error &error)
{
    if (m_pending && m_pending->stage == Stage::Storing)
        fail(error.message, error.detail, false);
}

void ConnectionController::onStoreRemoved(const QUuid &id)
{
    const auto it = m_removing.constFind(id);
    if (it == m_removing.constEnd())
        return;
    const PasswordMode mode = it.value();
    m_removing.erase(it);
    if (mode == PasswordMode::Saved)
        m_backend.remove(id);
    m_backend.forget(id);
    emit connectionRemoved(toId(id));
}

void ConnectionController::onPasswordDeleted(QUuid) { }

void ConnectionController::readPassword(QUuid id)
{
    m_externalReads.insert(id);
    m_backend.read(id);
}

void ConnectionController::onCredentialFailed(QUuid id, const Error &error)
{
    const bool external = m_externalReads.remove(id);
    if (external)
        emit passwordUnavailable(id, error);
    if (m_tree.onPasswordFailed(id, error) || external)
        return;
    if (m_pending && m_pending->stage == Stage::WritingPassword && m_pending->profile.id == id) {
        fail(error.message, error.detail, true);
    } else if (m_testReadId == id && !id.isNull()) {
        m_testReadId = QUuid();
        setTest(QStringLiteral("failed"), error.message, {}, error.detail);
    } else {
        // Deleting the keychain entry of a removed connection failed: the connection is gone,
        // the user is told the entry may remain.
        setError(error.message, error.detail);
    }
}

// ---- test connection ---------------------------------------------------------------------------

void ConnectionController::setTest(const QString &state, const QString &message,
                                   const QString &hint, const QString &detail)
{
    m_testState = state;
    m_testMessage = message;
    m_testHint = hint;
    m_testDetail = detail;
    emit testChanged();
}

bool ConnectionController::testConnection(const QVariantMap &draft)
{
    ensureRegistry();
    if (!m_form.isValid() || draft.value(QStringLiteral("providerId")).toString().isEmpty())
        return false;
    m_testProfile = profileFrom(draft);
    m_testPassword = draft.value(QStringLiteral("password")).toString();
    setTest(QStringLiteral("testing"));
    const bool touched = draft.value(QStringLiteral("passwordTouched")).toBool();
    const bool saved = m_testProfile.passwordMode == PasswordMode::Saved && !touched
        && !m_testProfile.id.isNull() && m_model.profile(m_testProfile.id).has_value();
    if (saved) {
        // The password field is untouched: the real one is in the keychain.
        m_testReadId = m_testProfile.id;
        m_backend.read(m_testReadId);
    } else {
        runTest();
    }
    return true;
}

void ConnectionController::onPasswordRead(QUuid id, QString password)
{
    m_tree.onPasswordRead(id, password);
    if (m_externalReads.remove(id))
        emit passwordAvailable(id, password);
    if (m_testReadId.isNull() || id != m_testReadId)
        return;
    m_testReadId = QUuid();
    m_testPassword = std::move(password);
    runTest();
}

void ConnectionController::runTest()
{
    m_tester.test(m_testProfile, Secrets { std::exchange(m_testPassword, QString()) });
}

void ConnectionController::cancelTest()
{
    m_testReadId = QUuid();
    if (m_testState == QLatin1String("testing")) {
        m_tester.cancel();
        setTest(QStringLiteral("idle")); // a read in flight has nothing to cancel
    }
}

void ConnectionController::resetTest()
{
    cancelTest();
    if (m_testState != QLatin1String("idle"))
        setTest(QStringLiteral("idle"));
}

// ---- delete, duplicate, open file --------------------------------------------------------------

void ConnectionController::remove(const QString &id)
{
    const QUuid uuid = QUuid::fromString(id);
    if (uuid.isNull())
        return;
    const auto profile = m_model.profile(uuid);
    // Unknown profile: still clear the keychain, in case an entry was left behind.
    m_removing.insert(uuid, profile ? profile->passwordMode : PasswordMode::Saved);
    connectStore();
    m_model.store()->remove(uuid);
}

void ConnectionController::duplicate(const QString &id)
{
    const auto source = m_model.profile(QUuid::fromString(id));
    if (!source || m_pending)
        return;
    Pending pending;
    pending.kind = Kind::Duplicate;
    pending.profile = *source;
    pending.profile.id = QUuid::createUuid();
    pending.profile.name = tr("%1 (copy)").arg(source->name);
    pending.profile.lastUsedAt.reset();
    m_pending = std::move(pending);
    connectStore();
    emit savingChanged();
    storeNow({}); // the password is deliberately not copied
}

void ConnectionController::openSqliteFile(const QUrl &file)
{
    ensureRegistry();
    const bool hasSqlite = std::ranges::any_of(m_registry.available(), [](const ProviderEntry &e) {
        return e.id == QLatin1String("sqlite");
    });
    if (!hasSqlite) {
        setError(tr("The SQLite engine is not available."), {});
        return;
    }
    const QString path = toLocalPath(file);
    if (path.isEmpty() || m_pending)
        return;
    // Opening a file that is already saved selects the saved connection.
    for (int i = 0; i < m_model.count(); ++i) {
        const QModelIndex row = m_model.index(i);
        if (m_model.data(row, ConnectionListModel::ProviderIdRole).toString()
            != QLatin1String("sqlite"))
            continue;
        const auto existing = m_model.profile(
            QUuid::fromString(m_model.data(row, ConnectionListModel::IdRole).toString()));
        if (existing && existing->values.value(QStringLiteral("file")).toString() == path) {
            emit connectionSaved(toId(existing->id));
            return;
        }
    }
    Pending pending;
    pending.kind = Kind::Open;
    pending.profile.id = QUuid::createUuid();
    pending.profile.name = QFileInfo(path).fileName();
    pending.profile.providerId = QStringLiteral("sqlite");
    pending.profile.values = { { QStringLiteral("file"), path } };
    m_pending = std::move(pending);
    connectStore();
    emit savingChanged();
    storeNow({});
}

void ConnectionController::setError(const QString &message, const QString &detail)
{
    m_errorMessage = message;
    m_errorDetail = detail;
    emit errorChanged();
}

QString ConnectionController::toLocalPath(const QUrl &url) const
{
    return QDir::toNativeSeparators(url.isLocalFile() ? url.toLocalFile() : url.toString());
}

void ConnectionController::clearError()
{
    if (!m_errorMessage.isEmpty() || !m_errorDetail.isEmpty())
        setError({}, {});
}

} // namespace quarry::ui
