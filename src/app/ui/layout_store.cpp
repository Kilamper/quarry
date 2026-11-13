#include "layout_store.h"

#include <QSettings>

namespace quarry::ui {

namespace {
QString fullKey(const QString &key)
{
    return QLatin1String("ui/layout/") + key;
}
} // namespace

QVariant LayoutStore::value(const QString &key, const QVariant &fallback) const
{
    return QSettings().value(fullKey(key), fallback);
}

void LayoutStore::setValue(const QString &key, const QVariant &value)
{
    QSettings().setValue(fullKey(key), value);
}

void LayoutStore::remove(const QString &key)
{
    QSettings().remove(fullKey(key));
}

} // namespace quarry::ui
