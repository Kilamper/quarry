#include <quarry/provider_registry.h>

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QStyleHints>
#include <QTimer>

#include <cstdio>

Q_LOGGING_CATEGORY(lcApp, "quarry.app")

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Quarry"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("quarry.dev"));
    QCoreApplication::setApplicationName(QStringLiteral("Quarry"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.0.1"));

    // Keyboard-only use is a requirement: every control takes Tab focus on every platform.
    QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);

    const bool smoke = app.arguments().contains(QStringLiteral("--smoke"));

    // shortcut: synchronous scan on the UI thread, move to a worker when plugins are numerous (H1).
    quarry::ProviderRegistry registry;
    registry.scan(quarry::ProviderRegistry::defaultPluginDirectory());
    qCInfo(lcApp) << "Providers available:" << registry.available().size();

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);

    // Any QML warning means the toolchain or deployment is broken. A flag, not exit():
    // warnings raised while loading arrive before exec(), where exit() is a no-op.
    bool qmlWarnings = false;
    if (smoke) {
        QObject::connect(
            &engine, &QQmlEngine::warnings, &app, [&qmlWarnings](const QList<QQmlError> &errors) {
                for (const QQmlError &e : errors)
                    std::fprintf(stderr, "QML warning: %s\n", qPrintable(e.toString()));
                qmlWarnings = true;
            });
    }

    engine.loadFromModule("Quarry", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    if (smoke)
        QTimer::singleShot(500, &app, &QCoreApplication::quit);

    const int code = app.exec();
    return qmlWarnings ? 2 : code;
}
