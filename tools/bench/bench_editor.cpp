// bench_editor: typing in the SQL editor with a 10 000 line file and syntax highlighting
// (docs/PERFORMANCE.md: p95 <= 16 ms per key). It loads the real SqlEditor.qml with the real
// EditorTabsModel, the SQLite dialect and KSyntaxHighlighting, types at the start, in the middle
// and at the end of the file, and prints JSON metric lines:
//   keystroke  = key press -> text, highlighting, layout and the model updated
//   frame      = the same, plus the scene graph polish/sync/render of the window (grabWindow)
// Run it by hand (Qt's bin directory on PATH on Windows):
//   build/windows-release/bench_editor.exe [lines]
#include "ui/connection_controller.h"
#include "ui/editor_tabs_model.h"
#include "ui/sql_document.h"

#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cstdio>
#include <map>
#include <vector>

using namespace quarry::ui;

namespace {

QString bigText(int lines)
{
    QStringList out;
    out.reserve(lines);
    for (int i = 1; i <= lines; ++i) {
        if (i % 7 == 0)
            out << QStringLiteral("-- note %1: the next statement joins two tables").arg(i);
        else if (i % 5 == 0)
            out << QStringLiteral("    AND o.total > %1 AND c.name LIKE '%abc%' ORDER BY o.id;")
                       .arg(i);
        else
            out << QStringLiteral("SELECT id, name, 'text %1' AS label FROM t%1 AS o WHERE id = %1")
                       .arg(i);
    }
    return out.join(QLatin1Char('\n'));
}

double percentile(std::vector<double> &v, double q)
{
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[std::min(v.size() - 1, static_cast<std::size_t>(q * v.size()))];
}

void report(const char *metric, std::vector<double> &v)
{
    std::printf("{\"metric\":\"%s_p50\",\"value\":%.2f,\"unit\":\"ms\"}\n", metric,
                percentile(v, 0.5));
    std::printf("{\"metric\":\"%s_p95\",\"value\":%.2f,\"unit\":\"ms\"}\n", metric,
                percentile(v, 0.95));
    std::printf("{\"metric\":\"%s_max\",\"value\":%.2f,\"unit\":\"ms\"}\n", metric,
                v.empty() ? 0.0 : v.back());
}

} // namespace

// Types one key into the window the way the platform would deliver it.
static void sendKey(QWindow *window, Qt::Key key)
{
    QString text;
    switch (key) {
    case Qt::Key_Return:
        text = QStringLiteral("\r");
        break;
    case Qt::Key_Backspace:
        text = QStringLiteral("\b");
        break;
    case Qt::Key_Space:
        text = QStringLiteral(" ");
        break;
    default:
        text = QString(QChar(static_cast<char16_t>(key)).toLower());
        break;
    }
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, text);
    QCoreApplication::sendEvent(window, &press);
    QCoreApplication::sendEvent(window, &release);
}

static int run(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("QuarryBench"));
    QCoreApplication::setApplicationName(QStringLiteral("bench_editor"));
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    const int lines = argc > 1 ? QString::fromLocal8Bit(argv[1]).toInt() : 10000;

    ConnectionController controller;
    controller.setDatabasePath(dir.filePath(QStringLiteral("quarry.db")));
    controller.setProviderDirectory(QStringLiteral(QUARRY_PLUGIN_DIR));
    controller.start();
    QSignalSpy saved(&controller, &ConnectionController::connectionSaved);
    controller.openSqliteFile(QUrl::fromLocalFile(dir.filePath(QStringLiteral("bench.db"))));
    if (!saved.wait(10000)) {
        std::fprintf(stderr, "could not save the bench connection\n");
        return 1;
    }

    EditorTabsModel tabs;
    tabs.setDatabasePath(dir.filePath(QStringLiteral("quarry.db")));
    tabs.setConnections(&controller);
    tabs.openTab(saved.at(0).at(0).toString());
    tabs.setText(0, bigText(lines));

    QQmlEngine engine;
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(QUARRY_APP_QML_DIR "/SqlEditor.qml")));
    QQuickWindow window;
    window.resize(1000, 700);
    QObject *object = component.createWithInitialProperties(
        { { QStringLiteral("tabs"), QVariant::fromValue(&tabs) },
          { QStringLiteral("row"), 0 },
          { QStringLiteral("definition"), tabs.definitionOf(0) },
          { QStringLiteral("tabTitle"), tabs.titleOf(0) } });
    auto *editor = qobject_cast<QQuickItem *>(object);
    if (editor == nullptr) {
        std::fprintf(stderr, "SqlEditor.qml: %s\n", qPrintable(component.errorString()));
        return 1;
    }
    editor->setParentItem(window.contentItem());
    editor->setSize(window.size());
    window.show();
    if (!QTest::qWaitForWindowExposed(&window)) {
        std::fprintf(stderr, "window not exposed\n");
        return 1;
    }
    auto *edit = qvariant_cast<QQuickItem *>(editor->property("edit"));
    // BENCH_NO_HIGHLIGHT=1 measures the same typing without KSyntaxHighlighting, to tell the two
    // apart.
    if (qEnvironmentVariableIsSet("BENCH_NO_HIGHLIGHT"))
        for (auto *doc : editor->findChildren<SqlDocument *>())
            doc->attach(nullptr);
    edit->forceActiveFocus();
    // The highlighter's first pass over the file and the first frame are the "open" cost, not the
    // typing cost.
    QElapsedTimer open;
    open.start();
    QTest::qWait(50);
    window.grabWindow();
    std::printf("{\"metric\":\"editor_first_frame\",\"value\":%.2f,\"unit\":\"ms\"}\n",
                static_cast<double>(open.elapsed()));
    QTest::qWait(500); // the statement split is debounced; let it run once

    const QString text = edit->property("text").toString();
    const int middle = static_cast<int>(
        QStringView(text).left(text.size() / 2).lastIndexOf(QLatin1Char('\n')) + 1);
    const int starts[] = { 0, middle, static_cast<int>(text.size()) };
    const Qt::Key keys[]
        = { Qt::Key_A,         Qt::Key_B,         Qt::Key_Return, Qt::Key_C, Qt::Key_Backspace,
            Qt::Key_D,         Qt::Key_E,         Qt::Key_Space,  Qt::Key_F, Qt::Key_Backspace,
            Qt::Key_Backspace, Qt::Key_G,         Qt::Key_H,      Qt::Key_I, Qt::Key_J,
            Qt::Key_Return,    Qt::Key_Backspace, Qt::Key_K,      Qt::Key_L, Qt::Key_M };

    std::vector<double> keystroke;
    std::vector<double> frame;
    // BENCH_VERBOSE=1: the mean per position and key, to see what is slow.
    const bool verbose = qEnvironmentVariableIsSet("BENCH_VERBOSE");
    std::map<std::pair<int, int>, std::pair<double, int>> parts;
    int where = 0;
    QElapsedTimer timer;
    for (const int start : starts) {
        edit->setProperty("cursorPosition", start);
        window.grabWindow();
        for (int rep = 0; rep < 5; ++rep) {
            for (const Qt::Key key : keys) {
                timer.start();
                sendKey(&window, key);
                QCoreApplication::processEvents();
                const double afterKey = static_cast<double>(timer.nsecsElapsed()) / 1e6;
                window.grabWindow();
                frame.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
                keystroke.push_back(afterKey);
                auto &acc = parts[{ where, static_cast<int>(key) }];
                acc.first += afterKey;
                ++acc.second;
                QTest::qWait(2); // a typist pauses; the debounce timers get their turn
            }
        }
        ++where;
    }
    if (verbose)
        for (const auto &[k, v] : parts)
            std::fprintf(stderr, "pos %d key 0x%x mean %.2f ms\n", k.first, k.second,
                         v.first / v.second);
    std::printf("{\"metric\":\"editor_lines\",\"value\":%d,\"unit\":\"lines\"}\n",
                edit->property("lineCount").toInt());
    report("editor_keystroke", keystroke);
    report("editor_frame", frame);
    return 0;
}

int main(int argc, char *argv[])
{
    try {
        return run(argc, argv);
    } catch (...) {
        return 1;
    }
}
