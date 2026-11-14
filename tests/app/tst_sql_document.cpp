#include "ui/sql_document.h"

#include <QColor>
#include <QSettings>
#include <QSyntaxHighlighter>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QtTest>

using namespace quarry::ui;

// The SQL editor's highlighting (ROADMAP H1.5): the definitions KSyntaxHighlighting ships for
// SQLite ("SQL") and PostgreSQL ("SQL (PostgreSQL)"), colored with the Theme syntax tokens.
class TstSqlDocument : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    ThemeManager *m_theme = nullptr;

    // The foreground color of one character after highlighting; invalid = the default text color.
    static QColor colorAt(const QTextDocument &doc, qsizetype pos)
    {
        const QTextBlock block = doc.findBlock(static_cast<int>(pos));
        const int inBlock = static_cast<int>(pos) - block.position();
        for (const QTextLayout::FormatRange &r : block.layout()->formats()) {
            if (inBlock >= r.start && inBlock < r.start + r.length
                && r.format.hasProperty(QTextFormat::ForegroundBrush))
                return r.format.foreground().color();
        }
        return {};
    }

    static bool italicAt(const QTextDocument &doc, qsizetype pos)
    {
        const QTextBlock block = doc.findBlock(static_cast<int>(pos));
        const int inBlock = static_cast<int>(pos) - block.position();
        for (const QTextLayout::FormatRange &r : block.layout()->formats())
            if (inBlock >= r.start && inBlock < r.start + r.length)
                return r.format.fontItalic();
        return false;
    }

    QColor token(Token t) const { return QColor::fromRgba(m_theme->token(t)); }

    // Every character of [from, from + length) has this color.
    void verifyRange(const QTextDocument &doc, qsizetype from, qsizetype length,
                     const QColor &color, const char *what)
    {
        for (qsizetype i = from; i < from + length; ++i) {
            if (doc.toPlainText().at(i) == QLatin1Char('\n'))
                continue; // a line break belongs to no format
            QVERIFY2(colorAt(doc, i) == color,
                     qPrintable(QStringLiteral("%1: character %2 ('%3') is %4, expected %5")
                                    .arg(QLatin1String(what))
                                    .arg(i)
                                    .arg(doc.toPlainText().at(i))
                                    .arg(colorAt(doc, i).name(), color.name())));
        }
    }

    struct Highlighted
    {
        QTextDocument doc;
        SqlDocument sql;
    };

    void highlight(Highlighted &h, const QString &definition, const QString &text)
    {
        h.sql.setTheme(m_theme);
        h.sql.setDefinition(definition);
        h.sql.attach(&h.doc);
        h.doc.setPlainText(text);
        static_cast<QSyntaxHighlighter *>(h.sql.highlighter())->rehighlight();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("QuarryTest"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_sql_document"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
        m_theme = new ThemeManager(this);
        m_theme->setSystemStateOverride({ Qt::ColorScheme::Light });
        m_theme->setMode(ThemeManager::Mode::Light);
    }

    void sqliteKeywordsStringsNumbersAndComments()
    {
        const QString text
            = QStringLiteral("SELECT 'a;b' AS x, 42 -- note\nFROM t /* c */ WHERE x = 1;");
        Highlighted h;
        highlight(h, QStringLiteral("SQL"), text);
        verifyRange(h.doc, text.indexOf(QLatin1String("SELECT")), 6, token(Token::SynKeyword),
                    "SELECT");
        verifyRange(h.doc, text.indexOf(QLatin1String("'a;b'")), 5, token(Token::SynString),
                    "string");
        verifyRange(h.doc, text.indexOf(QLatin1String("42")), 2, token(Token::SynNumber), "number");
        verifyRange(h.doc, text.indexOf(QLatin1String("-- note")), 7, token(Token::SynComment),
                    "line comment");
        QVERIFY(italicAt(h.doc, text.indexOf(QLatin1String("-- note"))));
        verifyRange(h.doc, text.indexOf(QLatin1String("FROM")), 4, token(Token::SynKeyword),
                    "FROM");
        verifyRange(h.doc, text.indexOf(QLatin1String("/* c */")), 7, token(Token::SynComment),
                    "block comment");
        QVERIFY(
            !colorAt(h.doc, text.indexOf(QLatin1String("t /*"))).isValid()); // a name is plain text
    }

    // ROADMAP H1.5: `$$ ... $$` is one string, whatever is inside it.
    void postgresDollarQuoting()
    {
        const QString text
            = QStringLiteral("SELECT $$ it's; /* not a comment */ -- nor this $$, 1;");
        Highlighted h;
        highlight(h, QStringLiteral("SQL (PostgreSQL)"), text);
        const qsizetype from = text.indexOf(QLatin1String("$$"));
        const qsizetype close = text.lastIndexOf(QLatin1String("$$"));
        verifyRange(h.doc, text.indexOf(QLatin1String("SELECT")), 6, token(Token::SynKeyword),
                    "SELECT");
        // The delimiters are punctuation; everything between them is one string, comments and
        // semicolons included.
        verifyRange(h.doc, from, 2, token(Token::SynOperator), "opening $$");
        verifyRange(h.doc, from + 2, close - from - 2, token(Token::SynString), "$$ body");
        verifyRange(h.doc, close, 2, token(Token::SynOperator), "closing $$");
        // After the closing $$ the text is code again, and the line comment inside did not leak.
        verifyRange(h.doc, text.lastIndexOf(QLatin1Char('1')), 1, token(Token::SynNumber),
                    "number after");
    }

    // A tagged body over several lines: the definition highlights it as code (a function body),
    // and the text after the closing tag is code again, not swallowed by the body.
    void postgresDollarQuotingWithTagAcrossLines()
    {
        const QString text = QStringLiteral("CREATE FUNCTION f() RETURNS int AS $body$\nSELECT 1; "
                                            "-- x\n$body$ LANGUAGE sql;\nSELECT 2;");
        Highlighted h;
        highlight(h, QStringLiteral("SQL (PostgreSQL)"), text);
        const qsizetype open = text.indexOf(QLatin1String("$body$"));
        const qsizetype close = text.indexOf(QLatin1String("$body$ LANGUAGE"));
        QVERIFY(colorAt(h.doc, open) != token(Token::SynComment)); // delimiters are not a comment
        verifyRange(h.doc, text.indexOf(QLatin1String("SELECT 1")), 6, token(Token::SynKeyword),
                    "SELECT in the body");
        verifyRange(h.doc, text.indexOf(QLatin1String("-- x")), 4, token(Token::SynComment),
                    "comment in the body");
        verifyRange(h.doc, close + 7, 8, token(Token::SynKeyword), "LANGUAGE after the body");
        verifyRange(h.doc, text.lastIndexOf(QLatin1String("SELECT")), 6, token(Token::SynKeyword),
                    "next statement");
    }

    // ROADMAP H1.5: PostgreSQL block comments nest.
    void postgresNestedComments()
    {
        const QString text = QStringLiteral("/* outer /* inner */ still outer */ SELECT 1");
        Highlighted h;
        highlight(h, QStringLiteral("SQL (PostgreSQL)"), text);
        const qsizetype commentEnd = text.indexOf(QLatin1String("SELECT"));
        verifyRange(h.doc, 0, commentEnd - 1, token(Token::SynComment), "nested comment");
        verifyRange(h.doc, commentEnd, 6, token(Token::SynKeyword), "SELECT after the comment");
    }

    void postgresNestedCommentsAcrossLines()
    {
        const QString text = QStringLiteral("/* a\n/* b */\nstill a */\nSELECT 1");
        Highlighted h;
        highlight(h, QStringLiteral("SQL (PostgreSQL)"), text);
        const qsizetype select = text.indexOf(QLatin1String("SELECT"));
        verifyRange(h.doc, text.indexOf(QLatin1String("still")), 9, token(Token::SynComment),
                    "second line of comment");
        verifyRange(h.doc, select, 6, token(Token::SynKeyword), "SELECT after the comment");
    }

    void unknownDefinitionFallsBackToSql()
    {
        Highlighted h;
        highlight(h, QStringLiteral("No Such Dialect"), QStringLiteral("SELECT 1"));
        verifyRange(h.doc, 0, 6, token(Token::SynKeyword), "SELECT");
    }

    // A theme switch repaints with the new tokens, live.
    void followsTheme()
    {
        const QString text = QStringLiteral("SELECT 'x' -- c");
        Highlighted h;
        highlight(h, QStringLiteral("SQL"), text);
        const QColor lightKeyword = colorAt(h.doc, 0);
        m_theme->setMode(ThemeManager::Mode::Dark);
        verifyRange(h.doc, 0, 6, token(Token::SynKeyword), "dark SELECT");
        verifyRange(h.doc, text.indexOf(QLatin1String("-- c")), 4, token(Token::SynComment),
                    "dark comment");
        QVERIFY(colorAt(h.doc, 0) != lightKeyword);
        m_theme->setMode(ThemeManager::Mode::Light);
        verifyRange(h.doc, 0, 6, token(Token::SynKeyword), "light SELECT");
    }

    void lineAndColumn()
    {
        Highlighted h;
        highlight(h, QStringLiteral("SQL"), QStringLiteral("ab\ncde\n\nf"));
        QCOMPARE(h.sql.lineOf(0), 1);
        QCOMPARE(h.sql.columnOf(0), 1);
        QCOMPARE(h.sql.columnOf(2), 3); // end of the first line
        QCOMPARE(h.sql.lineOf(3), 2);
        QCOMPARE(h.sql.columnOf(5), 3);
        QCOMPARE(h.sql.lineOf(7), 3); // the empty line
        QCOMPARE(h.sql.lineOf(8), 4);
        QCOMPARE(h.sql.columnOf(9), 2); // after the last character
    }

    void outdentRemovesOneLevel()
    {
        Highlighted h;
        highlight(h, QStringLiteral("SQL"), QStringLiteral("\tone\n      two\n  three\nfour"));
        QCOMPARE(h.sql.outdent(1), 1);
        QCOMPARE(h.sql.outdent(8), 4);
        QCOMPARE(h.sql.outdent(h.doc.toPlainText().indexOf(QLatin1String("three"))), 2);
        QCOMPARE(h.sql.outdent(h.doc.toPlainText().indexOf(QLatin1String("four"))), 0);
        QCOMPARE(h.doc.toPlainText(), QStringLiteral("one\n  two\nthree\nfour"));
    }
};

QTEST_MAIN(TstSqlDocument)
#include "tst_sql_document.moc"
