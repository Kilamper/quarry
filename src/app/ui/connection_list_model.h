#pragma once

#include <quarry/connection_profile.h>
#include <quarry/connection_store.h>

#include <QAbstractListModel>
#include <QString>
#include <QUuid>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <optional>
#include <vector>

namespace quarry {
class ProviderRegistry;
}

namespace quarry::ui {

// Saved connections for the sidebar, kept sorted by name. No role ever carries a secret: only
// what ConnectionStore keeps (it strips passwords) and a summary built from non-secret values.
// The store (and its thread) is created by start(), which the UI calls after the first frame.
class ConnectionListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ConnectionController")

    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged FINAL)
    // Plain-language reason the list could not be loaded; empty when it could.
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged FINAL)
    Q_PROPERTY(QString lastErrorDetail READ lastErrorDetail NOTIFY lastErrorChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        ProviderIdRole,
        ProviderNameRole,
        SummaryRole,
        ColorIndexRole,
        ColorNameRole,
        ProductionRole,
        PasswordModeRole,
    };

    // Empty path = ConnectionStore::defaultDatabasePath(). Tests inject a temporary one.
    explicit ConnectionListModel(QString databasePath = {}, QObject *parent = nullptr);

    void setDatabasePath(const QString &path); // before store() creates the store
    void setRegistry(const quarry::ProviderRegistry *registry) { m_registry = registry; }

    // Creates the store on first use. The model's own handlers are connected first, so any
    // later connection to the store's signals sees the model already updated.
    quarry::ConnectionStore *store();
    // Creates the store if needed and asks it to load; the list fills in asynchronously.
    void start();

    bool loading() const { return m_loading; }
    QString lastError() const { return m_lastError; }
    QString lastErrorDetail() const { return m_lastErrorDetail; }
    int count() const { return static_cast<int>(m_rows.size()); }

    std::optional<quarry::ConnectionProfile> profile(const QUuid &id) const;
    Q_INVOKABLE int indexOf(const QString &id) const;
    // All roles of one row by name, for QML code that is not a delegate.
    Q_INVOKABLE QVariantMap get(int row) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Non-secret text under the name: file name, or host:port/database.
    static QString summaryOf(const quarry::ConnectionProfile &profile);

signals:
    void loadingChanged();
    void lastErrorChanged();
    void countChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void onLoaded(const QList<quarry::ConnectionProfile> &profiles);
    void onSaved(const quarry::ConnectionProfile &profile);
    void onRemoved(const QUuid &id);
    void onFailed(const quarry::Error &error);
    int insertionRow(const QString &name) const;
    int rowOf(const QUuid &id) const;
    QString providerName(const QString &providerId) const;

    QString m_path;
    quarry::ConnectionStore *m_store = nullptr;
    const quarry::ProviderRegistry *m_registry = nullptr;
    std::vector<quarry::ConnectionProfile> m_rows;
    bool m_loading = true; // until the first load ends, so the first frame is not "empty"
    QString m_lastError;
    QString m_lastErrorDetail;
};

} // namespace quarry::ui
