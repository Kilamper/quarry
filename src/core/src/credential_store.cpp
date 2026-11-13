#include <quarry/credential_store.h>

#include <qt6keychain/keychain.h>

namespace quarry {

namespace {

const QString kService = QStringLiteral("dev.quarry.Quarry");

QString accountFor(const QUuid &id)
{
    return QStringLiteral("connection/%1/password").arg(id.toString(QUuid::WithoutBraces));
}

Error toError(const QKeychain::Job &job)
{
    const QString detail = job.errorString();
    switch (job.error()) {
    case QKeychain::EntryNotFound:
        return { ErrorKind::NotFound,
                 CredentialStore::tr("No saved password was found for this connection."),
                 detail,
                 {},
                 {} };
    case QKeychain::AccessDenied:
    case QKeychain::AccessDeniedByUser:
        return { ErrorKind::Permission,
                 CredentialStore::tr("Access to the system keychain was denied."),
                 detail,
                 {},
                 {} };
    default: // NoBackendAvailable, NotImplemented, OtherError...
        return { ErrorKind::Unsupported,
                 CredentialStore::tr("The system keychain is not available. Choose \xE2\x80\x9C"
                                     "Ask every time\xE2\x80\x9D to enter the password when "
                                     "connecting."),
                 detail,
                 {},
                 {} };
    }
}

} // namespace

CredentialStore::CredentialStore(QObject *parent)
    : QObject(parent)
{ }

CredentialStore::~CredentialStore()
{
    // shortcut: QString::fill detaches a shared buffer, so copies held elsewhere (the caller's
    // argument, an old Secrets) are not wiped; the allocator may also have moved the data. Real
    // memory hygiene needs a locked, non-shared buffer; upgrade if the threat model asks for it.
    for (QString &password : m_session)
        password.fill(u'\0');
}

void CredentialStore::rememberForSession(QUuid id, QString password)
{
    forgetForSession(id);
    m_session.insert(id, std::move(password));
}

std::optional<QString> CredentialStore::sessionPassword(QUuid id) const
{
    const auto it = m_session.constFind(id);
    if (it == m_session.constEnd())
        return std::nullopt;
    return *it;
}

void CredentialStore::forgetForSession(QUuid id)
{
    const auto it = m_session.find(id);
    if (it == m_session.end())
        return;
    it->fill(u'\0'); // see the shortcut note in the destructor
    m_session.erase(it);
}

void CredentialStore::readPassword(QUuid id)
{
    auto *job = new QKeychain::ReadPasswordJob(kService, this);
    job->setAutoDelete(true);
    job->setKey(accountFor(id));
    connect(job, &QKeychain::Job::finished, this, [this, id](QKeychain::Job *j) {
        if (j->error() == QKeychain::NoError) {
            const auto *read = static_cast<QKeychain::ReadPasswordJob *>(j);
            emit passwordRead(id, read->textData());
        } else {
            emit failed(id, toError(*j));
        }
    });
    job->start();
}

void CredentialStore::writePassword(QUuid id, QString password)
{
    auto *job = new QKeychain::WritePasswordJob(kService, this);
    job->setAutoDelete(true);
    job->setKey(accountFor(id));
    job->setTextData(password);
    password.fill(u'\0'); // see the shortcut note in the destructor
    connect(job, &QKeychain::Job::finished, this, [this, id](QKeychain::Job *j) {
        if (j->error() == QKeychain::NoError)
            emit passwordWritten(id);
        else
            emit failed(id, toError(*j));
    });
    job->start();
}

void CredentialStore::deletePassword(QUuid id)
{
    auto *job = new QKeychain::DeletePasswordJob(kService, this);
    job->setAutoDelete(true);
    job->setKey(accountFor(id));
    connect(job, &QKeychain::Job::finished, this, [this, id](QKeychain::Job *j) {
        if (j->error() == QKeychain::NoError || j->error() == QKeychain::EntryNotFound)
            emit passwordDeleted(id);
        else
            emit failed(id, toError(*j));
    });
    job->start();
}

} // namespace quarry
