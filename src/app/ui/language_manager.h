#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QTranslator>
#include <QtQml/qqmlregistration.h>

namespace quarry::ui {

// QML singleton `Language`. Picks the UI language (setting `ui/language`), installs the matching
// translator and retranslates the QML engine. Source strings are English, so English installs
// nothing. main() builds one before the engine so the first frame is already translated.
class LanguageManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Language)
    QML_SINGLETON

    Q_PROPERTY(Choice choice READ choice WRITE setChoice NOTIFY choiceChanged FINAL)
    // What `choice` resolved to: English or Spanish, never System.
    Q_PROPERTY(Choice effective READ effective NOTIFY choiceChanged FINAL)

public:
    enum class Choice { System, English, Spanish };
    Q_ENUM(Choice)

    // No default argument, on purpose: a default-constructible singleton is built by QML without
    // create(), and main() must be able to hand the engine its own instance.
    explicit LanguageManager(QObject *parent);
    ~LanguageManager() override;

    // The instance main() created, else a new one owned by the engine.
    static LanguageManager *create(QQmlEngine *engine, QJSEngine *);

    Choice choice() const { return m_choice; }
    void setChoice(Choice choice);
    Choice effective() const { return m_effective; }

    // System follows the first supported entry of `uiLanguages` (QLocale::uiLanguages()).
    static Choice resolve(Choice choice, const QStringList &uiLanguages);

signals:
    void choiceChanged();

private:
    void apply();

    Choice m_choice = Choice::System;
    Choice m_effective = Choice::English;
    QTranslator m_translator;
    bool m_installed = false;
    QQmlEngine *m_engine = nullptr; // cleared when the engine goes away
};

} // namespace quarry::ui
