#include "ui/language_manager.h"

#include <quarry/provider_registry.h>

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickWindow>
#include <QStyleHints>
#include <QTimer>

#include <cstdio>

Q_LOGGING_CATEGORY(lcApp, "quarry.app")

int main(int argc, char *argv[])
{
    QElapsedTimer sinceStart; // for --bench-startup; started before anything else
    sinceStart.start();
    QGuiApplication app(argc, argv);
    // --bench-startup also prints where the time goes, to stderr.
    const bool bench = app.arguments().contains(QStringLiteral("--bench-startup"));
    const auto mark = [&](const char *what) {
        if (bench)
            std::fprintf(stderr, "TRACE %6lld ms %s\n",
                         static_cast<long long>(sinceStart.elapsed()), what);
    };
    mark("QGuiApplication");
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
    mark("providers scanned");
    qCInfo(lcApp) << "Providers available:" << registry.available().size();

    // Before the engine: the first frame is already translated. The engine reuses this instance.
    quarry::ui::LanguageManager language(nullptr);
    mark("language");

    QQmlApplicationEngine engine;
    mark("engine");
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
    mark("loadFromModule");
    if (engine.rootObjects().isEmpty())
        return 1;

    if (bench) {
        // Metric for docs/PERFORMANCE.md §2: process start (main) to the first presented frame.
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        if (window == nullptr)
            return 1;
        QObject::connect(
            window, &QQuickWindow::frameSwapped, &app,
            [&sinceStart, &app, &mark] {
                mark("first frame");
                std::printf("{\"metric\":\"startup_first_frame_ms\",\"value\":%lld}\n",
                            static_cast<long long>(sinceStart.elapsed()));
                std::fflush(stdout);
                app.quit();
            },
            Qt::SingleShotConnection);
        QTimer::singleShot(10000, &app, &QCoreApplication::quit); // never hang a CI job
    }

    if (smoke)
        QTimer::singleShot(500, &app, &QCoreApplication::quit);

    const int code = app.exec();
    return qmlWarnings ? 2 : code;
}
