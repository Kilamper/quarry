#pragma once

#include <QByteArray>
#include <QEvent>
#include <QGuiApplication>
#include <QObject>
#include <QRect>
#include <QScreen>
#include <QWindow>

#include <algorithm>

namespace quarry::testing {

// Opt-in for a developer with several monitors: QUARRY_TEST_SCREEN=<index into
// QGuiApplication::screens()> opens every top-level window of a test or bench run on that screen
// instead of the primary one. Unset or out of range: nothing changes (CI, other machines).
class ScreenPlacer : public QObject
{
public:
    // Call after the QGuiApplication exists; the filter lives as long as the application.
    static void install()
    {
        bool ok = false;
        const int index = qEnvironmentVariable("QUARRY_TEST_SCREEN").toInt(&ok);
        const auto screens = QGuiApplication::screens();
        if (!ok || index < 0 || index >= screens.size())
            return;
        qApp->installEventFilter(new ScreenPlacer(screens.at(index), qApp));
    }

private:
    ScreenPlacer(QScreen *screen, QObject *parent)
        : QObject(parent)
        , m_screen(screen)
    { }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::Show)
            return false;
        auto *window = qobject_cast<QWindow *>(watched);
        if (window == nullptr || window->parent() != nullptr || m_placed.contains(window))
            return false;
        m_placed.append(window);
        connect(window, &QObject::destroyed, this, [this, window] { m_placed.removeOne(window); });
        if (window->screen() == m_screen)
            return false;
        window->setScreen(m_screen);
        // The size is kept, even when it is larger than the screen; only the corner is moved.
        const QRect area = m_screen->availableGeometry();
        window->setPosition(
            area.topLeft() + QPoint(std::min(40, std::max(0, area.width() - window->width())), 40));
        return false;
    }

    QScreen *m_screen;
    QList<QWindow *> m_placed;
};

} // namespace quarry::testing
