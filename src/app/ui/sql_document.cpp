#include "sql_document.h"

#include <QCoreApplication>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Theme>
#include <memory>

namespace quarry::ui {

namespace {

// One Repository for the whole app, created by the first editor that needs it (never at
// startup: ARCHITECTURE section 12) and released before the application object goes away.
KSyntaxHighlighting::Repository &repository()
{
    static std::unique_ptr<KSyntaxHighlighting::Repository> repo;
    if (!repo) {
        repo = std::make_unique<KSyntaxHighlighting::Repository>();
        qAddPostRoutine([] { repo.reset(); });
    }
    return *repo;
}

KSyntaxHighlighting::Definition definitionNamed(const QString &name)
{
    auto &repo = repository();
    auto def = repo.definitionForName(name);
    return def.isValid() ? def : repo.definitionForName(QStringLiteral("SQL"));
}

} // namespace

// Colors from the Theme syntax tokens instead of a KSyntaxHighlighting theme file: the tokens
// are the single, contrast-tested source of colors, and a theme switch only repaints.
class SqlHighlighter : public KSyntaxHighlighting::SyntaxHighlighter
{
public:
    using KSyntaxHighlighting::SyntaxHighlighter::SyntaxHighlighter;

    void setThemeManager(ThemeManager *theme) { m_theme = theme; }

protected:
    void applyFormat(int offset, int length, const KSyntaxHighlighting::Format &format) override
    {
        if (!format.isValid() || m_theme == nullptr)
            return;
        using Style = KSyntaxHighlighting::Theme::TextStyle;
        Token token = Token::FgDefault;
        bool italic = false;
        switch (format.textStyle()) {
        case Style::Keyword:
        case Style::ControlFlow:
        case Style::Import:
        case Style::Preprocessor:
        case Style::Extension:
            token = Token::SynKeyword;
            break;
        case Style::Function:
        case Style::BuiltIn:
            token = Token::SynFunction;
            break;
        case Style::String:
        case Style::Char:
        case Style::SpecialChar:
        case Style::VerbatimString:
        case Style::SpecialString:
            token = Token::SynString;
            break;
        case Style::DecVal:
        case Style::BaseN:
        case Style::Float:
        case Style::Constant:
            token = Token::SynNumber;
            break;
        case Style::DataType:
            token = Token::SynType;
            break;
        case Style::Operator:
            token = Token::SynOperator;
            break;
        case Style::Comment:
        case Style::Documentation:
        case Style::Annotation:
        case Style::CommentVar:
        case Style::RegionMarker:
            token = Token::SynComment;
            italic = true; // comments differ from code by more than color
            break;
        case Style::Error:
        case Style::Alert:
            token = Token::Danger;
            break;
        default:
            return; // normal text keeps the editor's own color
        }
        QTextCharFormat out;
        out.setForeground(QColor::fromRgba(m_theme->token(token)));
        if (italic)
            out.setFontItalic(true);
        setFormat(offset, length, out);
    }

private:
    ThemeManager *m_theme = nullptr; // SqlDocument tells us when it goes away
};

SqlDocument::SqlDocument(QObject *parent)
    : QObject(parent)
{ }

SqlDocument::~SqlDocument()
{
    dropHighlighter(); // an editor that goes away must stop highlighting its document
}

void SqlDocument::dropHighlighter()
{
    if (auto *hl = static_cast<SqlHighlighter *>(m_highlighter)) {
        hl->disconnect(this);
        hl->setDocument(nullptr);
        hl->deleteLater();
    }
    m_highlighter = nullptr;
}

void SqlDocument::setTextDocument(QQuickTextDocument *document)
{
    if (document == m_quickDocument)
        return;
    if (m_quickDocument)
        m_quickDocument->disconnect(this);
    m_quickDocument = document;
    if (document)
        connect(document, &QObject::destroyed, this, [this] { m_quickDocument = nullptr; });
    attach(document != nullptr ? document->textDocument() : nullptr);
    emit textDocumentChanged();
}

void SqlDocument::attach(QTextDocument *document)
{
    if (document == m_document && (document == nullptr || m_highlighter != nullptr))
        return;
    dropHighlighter();
    if (m_document)
        m_document->disconnect(this);
    m_document = document;
    if (document == nullptr)
        return;
    connect(document, &QObject::destroyed, this, [this] { m_document = nullptr; });
    m_highlighter = new SqlHighlighter(document);
    connect(m_highlighter, &QObject::destroyed, this, [this] { m_highlighter = nullptr; });
    static_cast<SqlHighlighter *>(m_highlighter)->setThemeManager(m_theme);
    applyDefinition();
}

void SqlDocument::applyDefinition()
{
    auto *hl = static_cast<SqlHighlighter *>(m_highlighter);
    if (hl == nullptr)
        return;
    hl->setDefinition(definitionNamed(m_definition));
}

void SqlDocument::setDefinition(const QString &name)
{
    const QString effective = name.isEmpty() ? QStringLiteral("SQL") : name;
    if (effective == m_definition)
        return;
    m_definition = effective;
    applyDefinition(); // setDefinition() rehighlights
    emit definitionChanged();
}

void SqlDocument::setTheme(ThemeManager *theme)
{
    if (theme == m_theme)
        return;
    if (m_theme)
        m_theme->disconnect(this);
    m_theme = theme;
    if (m_theme) {
        connect(m_theme, &QObject::destroyed, this, [this] {
            m_theme = nullptr;
            if (m_highlighter)
                static_cast<SqlHighlighter *>(m_highlighter)->setThemeManager(nullptr);
        });
        connect(m_theme, &ThemeManager::themeChanged, this, [this] {
            if (auto *hl = static_cast<SqlHighlighter *>(m_highlighter))
                hl->rehighlight();
        });
    }
    if (auto *hl = static_cast<SqlHighlighter *>(m_highlighter)) {
        hl->setThemeManager(m_theme);
        hl->rehighlight();
    }
    emit themeChanged();
}

int SqlDocument::lineOf(int position) const
{
    return m_document ? m_document->findBlock(position).blockNumber() + 1 : 1;
}

int SqlDocument::columnOf(int position) const
{
    if (!m_document)
        return 1;
    const QTextBlock block = m_document->findBlock(position);
    return block.isValid() ? position - block.position() + 1 : 1;
}

int SqlDocument::outdent(int position)
{
    if (!m_document)
        return 0;
    const QTextBlock block = m_document->findBlock(position);
    if (!block.isValid())
        return 0;
    const QString text = block.text();
    int n = 0;
    if (text.startsWith(QLatin1Char('\t'))) {
        n = 1;
    } else {
        while (n < 4 && n < text.size() && text[n] == QLatin1Char(' '))
            ++n;
    }
    if (n > 0) {
        QTextCursor cursor(block);
        cursor.setPosition(block.position() + n, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
    return n;
}

} // namespace quarry::ui
