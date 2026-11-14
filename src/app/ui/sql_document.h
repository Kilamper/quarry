#pragma once

#include "theme_manager.h"

#include <QObject>
#include <QQuickTextDocument>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QTextDocument;

namespace quarry::ui {

// What the SQL editor needs from its QTextDocument: syntax highlighting by the dialect's
// KSyntaxHighlighting definition, colored with the Theme syntax tokens (so it follows theme
// switches live), and line/column of a position. Attach one to a TextEdit:
//   SqlDocument { textDocument: edit.textDocument; definition: "SQL"; theme: Theme }
class SqlDocument : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QQuickTextDocument *textDocument READ textDocument WRITE setTextDocument NOTIFY
                   textDocumentChanged FINAL)
    // KSyntaxHighlighting definition name; one that does not exist falls back to "SQL".
    Q_PROPERTY(
        QString definition READ definition WRITE setDefinition NOTIFY definitionChanged FINAL)
    Q_PROPERTY(quarry::ui::ThemeManager *theme READ theme WRITE setTheme NOTIFY themeChanged FINAL)

public:
    explicit SqlDocument(QObject *parent = nullptr);
    ~SqlDocument() override;

    QQuickTextDocument *textDocument() const { return m_quickDocument; }
    void setTextDocument(QQuickTextDocument *document);
    QString definition() const { return m_definition; }
    void setDefinition(const QString &name);
    ThemeManager *theme() const { return m_theme; }
    void setTheme(ThemeManager *theme);

    // Attaches to a plain document (tests, bench); QML uses textDocument.
    void attach(QTextDocument *document);

    // 1-based line and column (UTF-16 units) of a character position.
    Q_INVOKABLE int lineOf(int position) const;
    Q_INVOKABLE int columnOf(int position) const;
    // Removes one level of indentation (a tab, or up to four spaces) from the line holding
    // `position`. Returns how many characters were removed.
    Q_INVOKABLE int outdent(int position);

    // Test access: the KSyntaxHighlighting::SyntaxHighlighter, null until a document is attached.
    QObject *highlighter() const { return m_highlighter; }

signals:
    void textDocumentChanged();
    void definitionChanged();
    void themeChanged();

private:
    void applyDefinition();
    void dropHighlighter();

    // Plain pointers: each is cleared by its own destroyed() signal (QPointer trips a false
    // positive of clang-analyzer in Qt's headers).
    QQuickTextDocument *m_quickDocument = nullptr;
    QTextDocument *m_document = nullptr;
    ThemeManager *m_theme = nullptr;
    QString m_definition = QStringLiteral("SQL");
    QObject *m_highlighter = nullptr; // child of the document, so it dies with it
};

} // namespace quarry::ui
