#pragma once

#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <functional>

namespace quarry::ui {

// One action. Declared in QML (`Command { commandId: ...; onTriggered: ... }`) or built in C++
// with setCallback(). Titles are QML bindings over qsTr(), so they follow language changes.
class Command : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // `id` is reserved in QML, hence commandId.
    Q_PROPERTY(QString commandId READ commandId WRITE setCommandId NOTIFY changed FINAL)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY changed FINAL)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY changed FINAL)
    // Portable QKeySequence text ("Ctrl+K"; Ctrl is Cmd on macOS) and one optional alternative.
    Q_PROPERTY(QString shortcut READ shortcut WRITE setShortcut NOTIFY changed FINAL)
    Q_PROPERTY(QString alternateShortcut READ alternateShortcut WRITE setAlternateShortcut NOTIFY
                   changed FINAL)
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY changed FINAL)
    Q_PROPERTY(bool checkable READ isCheckable WRITE setCheckable NOTIFY changed FINAL)
    Q_PROPERTY(bool checked READ isChecked WRITE setChecked NOTIFY changed FINAL)
    // False for commands that must not enter the recent list (the one that opens the palette).
    Q_PROPERTY(bool trackRecent READ trackRecent WRITE setTrackRecent NOTIFY changed FINAL)
    Q_PROPERTY(QString shortcutText READ shortcutText NOTIFY changed FINAL)
    Q_PROPERTY(QStringList sequences READ sequences NOTIFY changed FINAL)

public:
    explicit Command(QObject *parent = nullptr)
        : QObject(parent)
    { }
    // Announces itself while still intact: QObject::destroyed comes too late for models.
    ~Command() override { emit aboutToBeDestroyed(this); }

    QString commandId() const { return m_id; }
    void setCommandId(const QString &v) { assign(m_id, v); }
    QString title() const { return m_title; }
    void setTitle(const QString &v) { assign(m_title, v); }
    QString category() const { return m_category; }
    void setCategory(const QString &v) { assign(m_category, v); }
    QString shortcut() const { return m_shortcut; }
    void setShortcut(const QString &v) { assign(m_shortcut, v); }
    QString alternateShortcut() const { return m_alternate; }
    void setAlternateShortcut(const QString &v) { assign(m_alternate, v); }
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool v) { assign(m_enabled, v); }
    bool isCheckable() const { return m_checkable; }
    void setCheckable(bool v) { assign(m_checkable, v); }
    bool isChecked() const { return m_checked; }
    void setChecked(bool v) { assign(m_checked, v); }

    bool trackRecent() const { return m_trackRecent; }
    void setTrackRecent(bool v) { assign(m_trackRecent, v); }

    // Shortcut as the user's platform writes it (Cmd on macOS).
    QString shortcutText() const;
    // Every non-empty sequence, for QML Shortcut items.
    QStringList sequences() const;

    // Runs the callback, then emits triggered(). Does not check `enabled`; the registry does.
    Q_INVOKABLE void trigger();
    void setCallback(std::function<void()> callback) { m_callback = std::move(callback); }

signals:
    void changed();
    void triggered();
    void aboutToBeDestroyed(quarry::ui::Command *command);

private:
    template<class T> void assign(T &member, const T &value)
    {
        if (member == value)
            return;
        member = value;
        emit changed();
    }

    QString m_id;
    QString m_title;
    QString m_category;
    QString m_shortcut;
    QString m_alternate;
    bool m_enabled = true;
    bool m_checkable = false;
    bool m_checked = false;
    bool m_trackRecent = true;
    std::function<void()> m_callback;
};

} // namespace quarry::ui
