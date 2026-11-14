#pragma once

#include "connection_form_model.h"
#include "connection_list_model.h"
#include "schema_tree_model.h"

#include <quarry/connection_profile.h>
#include <quarry/connection_tester.h>
#include <quarry/credential_store.h>
#include <quarry/provider_registry.h>

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>

namespace quarry::ui {

// What the controller needs from the keychain. The default talks to CredentialStore; tests
// replace it so they never touch the real keychain.
struct CredentialBackend
{
    std::function<void(QUuid, QString)> write;
    std::function<void(QUuid)> read;
    std::function<void(QUuid)> remove;
    std::function<void(QUuid)> forget;
};

// Orchestrates the connection screens: create/edit/duplicate/delete over ConnectionStore, the
// password per mode over CredentialStore, "Test connection" over ConnectionTester, and the form
// fields of the engine being edited. Every operation is asynchronous and ends in a signal.
//
// Draft maps (from the dialog): id ("" = new), providerId, name, colorIndex, production,
// passwordMode (0 none, 1 saved, 2 ask every time), password, passwordTouched.
class ConnectionController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(
        QString databasePath READ databasePath WRITE setDatabasePath NOTIFY configChanged FINAL)
    Q_PROPERTY(QString providerDirectory READ providerDirectory WRITE setProviderDirectory NOTIFY
                   configChanged FINAL)
    Q_PROPERTY(quarry::ui::ConnectionListModel *model READ model CONSTANT FINAL)
    Q_PROPERTY(quarry::ui::ConnectionFormModel *form READ form CONSTANT FINAL)
    Q_PROPERTY(quarry::ui::SchemaTreeModel *tree READ tree CONSTANT FINAL)
    Q_PROPERTY(QVariantList engines READ engines NOTIFY enginesChanged FINAL)
    Q_PROPERTY(bool started READ started NOTIFY startedChanged FINAL)
    Q_PROPERTY(bool saving READ saving NOTIFY savingChanged FINAL)
    // Message of the last failed operation outside the dialog (delete, duplicate, open file).
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged FINAL)
    Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY errorChanged FINAL)
    // "idle" | "testing" | "success" | "failed"
    Q_PROPERTY(QString testState READ testState NOTIFY testChanged FINAL)
    Q_PROPERTY(QString testMessage READ testMessage NOTIFY testChanged FINAL)
    Q_PROPERTY(QString testHint READ testHint NOTIFY testChanged FINAL)
    Q_PROPERTY(QString testDetail READ testDetail NOTIFY testChanged FINAL)

public:
    explicit ConnectionController(QObject *parent = nullptr);

    QString databasePath() const { return m_databasePath; }
    void setDatabasePath(const QString &path);
    QString providerDirectory() const { return m_providerDirectory; }
    void setProviderDirectory(const QString &dir);

    ConnectionListModel *model() { return &m_model; }
    ConnectionFormModel *form() { return &m_form; }
    SchemaTreeModel *tree() { return &m_tree; }
    QVariantList engines() const { return m_engines; }
    bool started() const { return m_started; }
    bool saving() const { return m_pending.has_value(); }
    QString errorMessage() const { return m_errorMessage; }
    QString errorDetail() const { return m_errorDetail; }
    QString testState() const { return m_testState; }
    QString testMessage() const { return m_testMessage; }
    QString testHint() const { return m_testHint; }
    QString testDetail() const { return m_testDetail; }

    // Secret keys given to the last ConnectionStore::save() (observed by tests).
    QStringList lastSecretKeys() const { return m_lastSecretKeys; }
    void setCredentialBackend(CredentialBackend backend) { m_backend = std::move(backend); }
    quarry::ProviderRegistry &registry() { return m_registry; }
    quarry::ConnectionTester &tester() { return m_tester; }

    // Scans the engines (metadata only) and starts loading the list. Idempotent. Called after
    // the first frame so neither the scan nor the store thread delays startup.
    Q_INVOKABLE void start();

    // Dialog support. prepare("") readies the form for a new connection; prepare(id) for
    // editing. Loading an engine's fields loads its plugin on the UI thread (a few ms, only
    // when the dialog is opened or the engine changes). Returns the initial values of the dialog.
    Q_INVOKABLE QVariantMap prepare(const QString &id);
    Q_INVOKABLE bool selectEngine(const QString &providerId);
    // False if the draft is invalid (the dialog shows why); otherwise starts saving and ends with
    // connectionSaved() or saveFailed().
    Q_INVOKABLE bool save(const QVariantMap &draft);
    // False if the form is invalid. Otherwise testState goes to "testing", then success/failed.
    Q_INVOKABLE bool testConnection(const QVariantMap &draft);
    Q_INVOKABLE void cancelTest();
    Q_INVOKABLE void resetTest();

    Q_INVOKABLE void remove(const QString &id);
    // Copies the connection as "<name> (copy)". The password is not copied.
    Q_INVOKABLE void duplicate(const QString &id);
    Q_INVOKABLE void openSqliteFile(const QUrl &file);
    Q_INVOKABLE void clearError();
    Q_INVOKABLE QString toLocalPath(const QUrl &url) const;

    // The keychain and the per-run password memory, shared with QueryController (the tree has its
    // own wiring). readPassword() answers with passwordAvailable() or passwordUnavailable().
    void readPassword(QUuid id);
    std::optional<QString> sessionPassword(QUuid id) const
    {
        return m_credentials.sessionPassword(id);
    }
    void rememberPassword(QUuid id, QString password)
    {
        m_credentials.rememberForSession(id, std::move(password));
    }
    void forgetPassword(QUuid id) { m_backend.forget(id); }

    // CredentialStore results; public so a test backend can deliver them.
public slots:
    void onPasswordRead(QUuid id, QString password);
    void onPasswordWritten(QUuid id);
    void onPasswordDeleted(QUuid id);
    void onCredentialFailed(QUuid id, const quarry::Error &error);

signals:
    void configChanged();
    void enginesChanged();
    void startedChanged();
    void savingChanged();
    void errorChanged();
    void testChanged();
    void connectionSaved(const QString &id);
    void saveFailed(const QString &message, const QString &detail, bool keychain);
    void connectionRemoved(const QString &id);
    void connectionDuplicated(const QString &id);
    void passwordAvailable(QUuid id, const QString &password);
    void passwordUnavailable(QUuid id, const quarry::Error &error);

private:
    enum class Kind { Save, Duplicate, Open };
    enum class Stage { WritingPassword, Storing };
    struct Pending
    {
        Kind kind = Kind::Save;
        Stage stage = Stage::Storing;
        quarry::ConnectionProfile profile;
        QString password;
        quarry::PasswordMode previousMode = quarry::PasswordMode::None;
    };

    void ensureRegistry();
    void connectStore();
    void storeNow(const QStringList &secretKeys);
    void endPending();
    void fail(const QString &message, const QString &detail, bool keychain);
    void setError(const QString &message, const QString &detail);
    void setTest(const QString &state, const QString &message = {}, const QString &hint = {},
                 const QString &detail = {});
    quarry::ConnectionProfile profileFrom(const QVariantMap &draft) const;
    QVariantMap draftOf(const quarry::ConnectionProfile &profile) const;
    void onStoreSaved(const quarry::ConnectionProfile &profile);
    void onStoreRemoved(const QUuid &id);
    void onStoreFailed(const quarry::Error &error);
    void runTest();

    QString m_databasePath;
    QString m_providerDirectory;
    ConnectionListModel m_model;
    ConnectionFormModel m_form;
    quarry::ProviderRegistry m_registry;
    quarry::ConnectionTester m_tester;
    quarry::CredentialStore m_credentials;
    CredentialBackend m_backend;
    SchemaTreeModel m_tree; // after the registry: the sessions' code lives in its plugins
    QVariantList m_engines;
    bool m_scanned = false;
    bool m_started = false;
    bool m_storeConnected = false;

    std::optional<Pending> m_pending;
    QSet<QUuid> m_externalReads; // keychain reads asked for through readPassword()
    QHash<QUuid, quarry::PasswordMode> m_removing; // deletions in flight, with the mode they had
    QStringList m_lastSecretKeys;

    QString m_errorMessage;
    QString m_errorDetail;

    QString m_testState = QStringLiteral("idle");
    QString m_testMessage;
    QString m_testHint;
    QString m_testDetail;
    quarry::ConnectionProfile m_testProfile;
    QString m_testPassword;
    QUuid m_testReadId; // non-null while the saved password is being read for a test
};

} // namespace quarry::ui
