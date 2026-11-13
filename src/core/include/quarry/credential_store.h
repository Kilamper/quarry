#pragma once

#include <quarry/result.h>
// Declares the Q_DECLARE_METATYPE for quarry::Error used by failed().
#include <quarry/session.h>

#include <QHash>
#include <QObject>
#include <QString>
#include <QUuid>

#include <optional>

namespace quarry {

// Passwords in the OS keychain (QtKeychain: Credential Manager, Keychain, Secret Service),
// service "dev.quarry.Quarry", account "connection/<uuid>/password". All keychain calls are
// asynchronous jobs; results arrive as signals on the UI thread. There is never a fallback to disk.
class CredentialStore : public QObject
{
    Q_OBJECT

public:
    explicit CredentialStore(QObject *parent = nullptr);
    // Overwrites the session passwords before releasing them (best effort).
    ~CredentialStore() override;

    // "Ask every time": passwords live only in this object's memory. Synchronous, no I/O.
    void rememberForSession(QUuid id, QString password);
    std::optional<QString> sessionPassword(QUuid id) const;
    void forgetForSession(QUuid id);

public slots:
    void readPassword(QUuid id); // -> passwordRead | failed (kind NotFound if absent)
    void writePassword(QUuid id, QString password); // -> passwordWritten | failed
    void deletePassword(QUuid id); // -> passwordDeleted (also if absent) | failed

signals:
    void passwordRead(QUuid id, QString password);
    void passwordWritten(QUuid id);
    void passwordDeleted(QUuid id);
    void failed(QUuid id, quarry::Error error);

private:
    QHash<QUuid, QString> m_session;
};

} // namespace quarry
