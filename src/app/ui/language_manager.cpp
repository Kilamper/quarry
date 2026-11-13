#include "language_manager.h"

#include <QCoreApplication>
#include <QLocale>
#include <QMetaEnum>
#include <QSettings>

namespace quarry::ui {

namespace {

constexpr QLatin1StringView kKeyLanguage { "ui/language" };
LanguageManager *g_instance = nullptr;

} // namespace

LanguageManager::LanguageManager(QObject *parent)
    : QObject(parent)
{
    g_instance = this;
    const QSettings s;
    bool ok = false;
    const int v = QMetaEnum::fromType<Choice>().keyToValue(
        s.value(kKeyLanguage).toString().toUtf8().constData(), &ok);
    m_choice = ok ? static_cast<Choice>(v) : Choice::System;
    apply();
}

LanguageManager::~LanguageManager()
{
    if (m_installed)
        QCoreApplication::removeTranslator(&m_translator);
    if (g_instance == this)
        g_instance = nullptr;
}

LanguageManager *LanguageManager::create(QQmlEngine *engine, QJSEngine *)
{
    LanguageManager *self = g_instance != nullptr ? g_instance : new LanguageManager(engine);
    self->m_engine = engine;
    connect(engine, &QObject::destroyed, self, [self] { self->m_engine = nullptr; });
    QJSEngine::setObjectOwnership(self, QJSEngine::CppOwnership);
    return self;
}

LanguageManager::Choice LanguageManager::resolve(Choice choice, const QStringList &uiLanguages)
{
    if (choice != Choice::System)
        return choice;
    for (const QString &lang : uiLanguages) {
        if (lang.startsWith(QLatin1String("es"), Qt::CaseInsensitive))
            return Choice::Spanish;
        if (lang.startsWith(QLatin1String("en"), Qt::CaseInsensitive))
            return Choice::English;
    }
    return Choice::English;
}

void LanguageManager::setChoice(Choice choice)
{
    if (choice == m_choice)
        return;
    m_choice = choice;
    QSettings().setValue(
        kKeyLanguage,
        QString::fromLatin1(QMetaEnum::fromType<Choice>().valueToKey(static_cast<int>(choice))));
    apply();
    emit choiceChanged();
    if (m_engine != nullptr)
        m_engine->retranslate();
}

void LanguageManager::apply()
{
    m_effective = resolve(m_choice, QLocale::system().uiLanguages());
    if (m_installed) {
        QCoreApplication::removeTranslator(&m_translator);
        m_installed = false;
    }
    if (m_effective == Choice::Spanish
        && m_translator.load(QStringLiteral(":/i18n/quarry_es.qm"))) {
        m_installed = QCoreApplication::installTranslator(&m_translator);
    }
}

} // namespace quarry::ui
