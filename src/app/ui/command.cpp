#include "command.h"

namespace quarry::ui {

QString Command::shortcutText() const
{
    return QKeySequence(m_shortcut, QKeySequence::PortableText).toString(QKeySequence::NativeText);
}

QStringList Command::sequences() const
{
    QStringList out;
    for (const QString &s : { m_shortcut, m_alternate })
        if (!s.isEmpty())
            out << s;
    return out;
}

void Command::trigger()
{
    if (m_callback)
        m_callback();
    emit triggered();
}

} // namespace quarry::ui
