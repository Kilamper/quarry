#include "connection_list_model.h"

#include "theme_tokens.h"

#include <quarry/provider_registry.h>

#include <QCoreApplication>
#include <QEvent>
#include <QFileInfo>

#include <algorithm>

namespace quarry::ui {

ConnectionListModel::ConnectionListModel(QString databasePath, QObject *parent)
    : QAbstractListModel(parent)
    , m_path(std::move(databasePath))
{
    if (QCoreApplication::instance() != nullptr)
        QCoreApplication::instance()->installEventFilter(this);
}

void ConnectionListModel::setDatabasePath(const QString &path)
{
    if (m_store == nullptr)
        m_path = path;
}

ConnectionStore *ConnectionListModel::store()
{
    if (m_store == nullptr) {
        m_store = new ConnectionStore(
            m_path.isEmpty() ? ConnectionStore::defaultDatabasePath() : m_path, this);
        connect(m_store, &ConnectionStore::loaded, this, &ConnectionListModel::onLoaded);
        connect(m_store, &ConnectionStore::saved, this, &ConnectionListModel::onSaved);
        connect(m_store, &ConnectionStore::removed, this, &ConnectionListModel::onRemoved);
        connect(m_store, &ConnectionStore::failed, this, &ConnectionListModel::onFailed);
    }
    return m_store;
}

void ConnectionListModel::start()
{
    const bool wasLoading = m_loading;
    m_loading = true;
    if (!wasLoading)
        emit loadingChanged();
    store()->load();
}

std::optional<ConnectionProfile> ConnectionListModel::profile(const QUuid &id) const
{
    const int row = rowOf(id);
    if (row < 0)
        return std::nullopt;
    return m_rows[static_cast<size_t>(row)];
}

int ConnectionListModel::indexOf(const QString &id) const
{
    return rowOf(QUuid::fromString(id));
}

QVariantMap ConnectionListModel::get(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= rowCount())
        return out;
    const auto names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        out.insert(QString::fromUtf8(it.value()), data(index(row), it.key()));
    return out;
}

int ConnectionListModel::rowOf(const QUuid &id) const
{
    for (size_t i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].id == id)
            return static_cast<int>(i);
    return -1;
}

int ConnectionListModel::insertionRow(const QString &name) const
{
    const auto it = std::ranges::find_if(m_rows, [&](const ConnectionProfile &p) {
        return QString::compare(p.name, name, Qt::CaseInsensitive) > 0;
    });
    return static_cast<int>(it - m_rows.begin());
}

void ConnectionListModel::onLoaded(const QList<ConnectionProfile> &profiles)
{
    beginResetModel();
    m_rows.assign(profiles.begin(), profiles.end());
    endResetModel();
    emit countChanged();
    m_loading = false;
    emit loadingChanged();
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        m_lastErrorDetail.clear();
        emit lastErrorChanged();
    }
}

void ConnectionListModel::onSaved(const ConnectionProfile &profile)
{
    const int old = rowOf(profile.id);
    if (old >= 0) {
        // Same neighbours in the same order: update in place, otherwise move the row.
        std::vector<ConnectionProfile> without;
        for (const auto &p : m_rows)
            if (p.id != profile.id)
                without.push_back(p);
        const auto target = std::ranges::find_if(without, [&](const ConnectionProfile &p) {
            return QString::compare(p.name, profile.name, Qt::CaseInsensitive) > 0;
        });
        if (target - without.begin() == old) {
            m_rows[static_cast<size_t>(old)] = profile;
            emit dataChanged(index(old), index(old));
            return;
        }
        beginRemoveRows({}, old, old);
        m_rows.erase(m_rows.begin() + old);
        endRemoveRows();
        emit countChanged();
    }
    const int row = insertionRow(profile.name);
    beginInsertRows({}, row, row);
    m_rows.insert(m_rows.begin() + row, profile);
    endInsertRows();
    emit countChanged();
}

void ConnectionListModel::onRemoved(const QUuid &id)
{
    const int row = rowOf(id);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_rows.erase(m_rows.begin() + row);
    endRemoveRows();
    emit countChanged();
}

void ConnectionListModel::onFailed(const Error &error)
{
    // Failures are not tied to an operation; only a load in progress is ours to report.
    if (!m_loading)
        return;
    m_lastError = error.message;
    m_lastErrorDetail = error.detail;
    emit lastErrorChanged();
    m_loading = false;
    emit loadingChanged();
}

QString ConnectionListModel::providerName(const QString &providerId) const
{
    if (m_registry != nullptr)
        for (const auto &entry : m_registry->available())
            if (entry.id == providerId)
                return entry.displayName;
    return providerId;
}

QString ConnectionListModel::summaryOf(const ConnectionProfile &profile)
{
    const QVariantMap &v = profile.values;
    if (v.contains(QStringLiteral("file"))) {
        const QString file = v.value(QStringLiteral("file")).toString();
        return file == QLatin1String(":memory:") ? file : QFileInfo(file).fileName();
    }
    QString out = v.value(QStringLiteral("host")).toString();
    const QString port = v.value(QStringLiteral("port")).toString();
    if (!out.isEmpty() && !port.isEmpty())
        out += QLatin1Char(':') + port;
    const QString db = v.value(QStringLiteral("database")).toString();
    if (!db.isEmpty())
        out += (out.isEmpty() ? QString() : QStringLiteral("/")) + db;
    return out;
}

int ConnectionListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant ConnectionListModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const ConnectionProfile &p = m_rows[static_cast<size_t>(index.row())];
    switch (role) {
    case IdRole:
        return p.id.toString(QUuid::WithoutBraces);
    case NameRole:
        return p.name;
    case ProviderIdRole:
        return p.providerId;
    case ProviderNameRole:
        return providerName(p.providerId);
    case SummaryRole:
        return summaryOf(p);
    case ColorIndexRole:
        return p.colorIndex;
    case ColorNameRole:
        return p.colorIndex >= 0 && p.colorIndex < kConnectionColorCount
            ? QCoreApplication::translate("Theme", connectionColorName(p.colorIndex))
            : QString();
    case ProductionRole:
        return p.production;
    case PasswordModeRole:
        return static_cast<int>(p.passwordMode);
    default:
        return {};
    }
}

QHash<int, QByteArray> ConnectionListModel::roleNames() const
{
    return {
        { IdRole, "connectionId" },           { NameRole, "name" },
        { ProviderIdRole, "providerId" },     { ProviderNameRole, "providerName" },
        { SummaryRole, "summary" },           { ColorIndexRole, "colorIndex" },
        { ColorNameRole, "colorName" },       { ProductionRole, "production" },
        { PasswordModeRole, "passwordMode" },
    };
}

bool ConnectionListModel::eventFilter(QObject *watched, QEvent *event)
{
    // Color names are translated in data(); tell the views when the language changes.
    if (event->type() == QEvent::LanguageChange && !m_rows.empty())
        emit dataChanged(index(0), index(rowCount() - 1), { ColorNameRole });
    return QAbstractListModel::eventFilter(watched, event);
}

} // namespace quarry::ui
