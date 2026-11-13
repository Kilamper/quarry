#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

namespace quarry::ui {

// QML singleton `LayoutStore`: window layout (splitter sizes, panel visibility) kept in
// QSettings under `ui/layout/<key>`. QML has no QSettings without an extra module.
class LayoutStore : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit LayoutStore(QObject *parent = nullptr)
        : QObject(parent)
    { }

    Q_INVOKABLE QVariant value(const QString &key, const QVariant &fallback) const;
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);
    Q_INVOKABLE void remove(const QString &key);
};

} // namespace quarry::ui
