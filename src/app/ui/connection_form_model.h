#pragma once

#include <quarry/provider_api.h>

#include <QAbstractListModel>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace quarry::ui {

// The fields of one engine's connection form (ConnectionField list of the provider), edited by
// the dialog. Secret fields are not rows: they are the single password control of the dialog and
// only their keys are kept (secretKeys()), to hand to ConnectionStore::save().
class ConnectionFormModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ConnectionController")

    Q_PROPERTY(bool hasAdvanced READ hasAdvanced NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(bool hasSecret READ hasSecret NOTIFY fieldsChanged FINAL)
    // No row has an error. Rows are checked live: the dialog decides when to show the message.
    Q_PROPERTY(bool valid READ isValid NOTIFY validChanged FINAL)

public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        LabelRole,
        KindRole, // "text" | "number" | "path" | "enum" | "bool"
        RequiredRole,
        AdvancedRole,
        EnumValuesRole,
        ValueRole, // number fields are edited as text
        ErrorRole, // plain message, empty when valid (computed from the value)
    };

    explicit ConnectionFormModel(QObject *parent = nullptr);

    void setFields(const std::vector<quarry::ConnectionField> &fields);
    // Fills the rows from saved values; keys the engine does not know are ignored.
    void setValues(const QVariantMap &values);
    // Typed values to store. Empty optional text/number fields are left out so defaults apply.
    QVariantMap values() const;
    QStringList secretKeys() const { return m_secretKeys; }
    bool hasSecret() const { return !m_secretKeys.isEmpty(); }
    bool hasAdvanced() const;

    Q_INVOKABLE void setValue(int row, const QVariant &value);
    bool isValid() const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void fieldsChanged();
    void validChanged();

private:
    struct Row
    {
        quarry::ConnectionField field;
        QVariant value;
    };

    static QString kindName(quarry::ConnectionField::Kind kind);
    QString errorFor(const Row &row) const;

    std::vector<Row> m_rows;
    QStringList m_secretKeys;
};

} // namespace quarry::ui
