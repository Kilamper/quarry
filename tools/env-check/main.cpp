#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QTimer>

#include <cstdio>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const bool smoke = app.arguments().contains(QStringLiteral("--smoke"));

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

    engine.loadFromModule("EnvCheck", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    if (smoke)
        QTimer::singleShot(500, &app, &QCoreApplication::quit);

    const int code = app.exec();
    return qmlWarnings ? 2 : code;
}
