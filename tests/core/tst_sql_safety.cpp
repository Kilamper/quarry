#include <quarry/sql_safety.h>

#include <QElapsedTimer>
#include <QtTest>

using namespace quarry;
using namespace Qt::StringLiterals;

namespace {
constexpr SqlRisk kNone = SqlRisk::None;
constexpr SqlRisk kUpd = SqlRisk::UpdateWithoutWhere;
constexpr SqlRisk kDel = SqlRisk::DeleteWithoutWhere;
constexpr SqlRisk kDrop = SqlRisk::Drop;
constexpr SqlRisk kTrunc = SqlRisk::Truncate;
} // namespace

class TstSqlSafety : public QObject
{
    Q_OBJECT

private:
    static void row(const char *name, const QString &sql, bool production, SqlRisk expected)
    {
        QTest::newRow(name) << sql << production << static_cast<int>(expected);
    }

private slots:
    void check_data()
    {
        QTest::addColumn<QString>("sql");
        QTest::addColumn<bool>("production");
        QTest::addColumn<int>("expected");

        // Flagged.
        row("delete", u"DELETE FROM t"_s, false, kDel);
        row("delete-comment-where", u"delete from t -- where"_s, false, kDel);
        row("delete-block-where", u"DELETE FROM t /* WHERE x */"_s, false, kDel);
        row("delete-semicolon", u"DELETE FROM t;  "_s, false, kDel);
        row("delete-trailing-ws", u"DELETE FROM t \n\t "_s, false, kDel);
        row("update-string-where", u"UPDATE t SET a = 'where'"_s, false, kUpd);
        row("update-ident-where", u"UPDATE \"where\" SET a = 1"_s, false, kUpd);
        row("update-backtick-where", u"UPDATE `where` SET a = 1"_s, false, kUpd);
        row("update-bracket-where", u"UPDATE [where] SET a = 1"_s, false, kUpd);
        row("cte-where-inside", u"WITH c AS (SELECT * FROM x WHERE y) DELETE FROM t"_s, false,
            kDel);
        row("using-subquery-where", u"DELETE FROM t USING (SELECT 1 WHERE true) s"_s, false, kDel);
        row("recursive-delete",
            u"WITH RECURSIVE r(n) AS (SELECT 1 UNION SELECT n+1 FROM r WHERE n < 5) DELETE FROM t"_s,
            false, kDel);
        row("recursive-update",
            u"WITH RECURSIVE r(n) AS (SELECT 1 WHERE true) UPDATE t SET a = 1"_s, false, kUpd);
        row("drop-prod", u"DROP TABLE t"_s, true, kDrop);
        row("drop-lower-prod", u"drop table t"_s, true, kDrop);
        row("truncate-prod", u"TRUNCATE t"_s, true, kTrunc);
        row("leading-comments", u"  -- hi\n /* x */ \n DELETE FROM t"_s, false, kDel);
        row("leading-nested-comment", u"/* a /* b */ c */ DELETE FROM t"_s, false, kDel);
        row("e-string-escape", u"UPDATE t SET a = E'it\\'s where' "_s, false, kUpd);
        row("dollar-tag-where", u"UPDATE t SET a = $x$ where $x$"_s, false, kUpd);
        row("dollar-body-quote", u"UPDATE t SET a = $$ ' where $$"_s, false, kUpd);
        row("string-doubled-quote", u"UPDATE t SET a = 'it''s where'"_s, false, kUpd);
        row("param-then-no-where", u"UPDATE t SET a = $1"_s, false, kUpd);
        row("unicode", u"DELETE FROM éè /* ü */"_s, false, kDel);
        row("explain-analyze", u"EXPLAIN ANALYZE DELETE FROM t"_s, false, kDel);
        row("explain-options", u"EXPLAIN (ANALYZE, VERBOSE) UPDATE t SET a = 1"_s, false, kUpd);
        row("with-update-nested-parens",
            u"WITH c AS (SELECT (1)) UPDATE t SET a = (SELECT 1 WHERE x)"_s, false, kUpd);

        // Not flagged.
        row("delete-where", u"DELETE FROM t WHERE id = 1"_s, false, kNone);
        row("delete-where-lower", u"delete from t where id = 1"_s, false, kNone);
        row("update-where", u"UPDATE t SET a = 1 WHERE b"_s, false, kNone);
        row("cte-delete-where",
            u"WITH c AS (SELECT 1) DELETE FROM t WHERE id IN (SELECT * FROM c)"_s, false, kNone);
        row("cte-select", u"WITH c AS (SELECT 1) SELECT * FROM c"_s, false, kNone);
        row("drop-dev", u"DROP TABLE t"_s, false, kNone);
        row("truncate-dev", u"TRUNCATE t"_s, false, kNone);
        row("alter-drop-column", u"ALTER TABLE t DROP COLUMN c"_s, true, kNone);
        row("select-string", u"SELECT 'DELETE FROM t'"_s, false, kNone);
        row("only-comment", u"-- DELETE FROM t"_s, false, kNone);
        row("only-block-comment", u"/* DELETE FROM t */"_s, false, kNone);
        row("dollar-quoted", u"$$DELETE FROM t$$"_s, false, kNone);
        row("create-trigger", u"CREATE TRIGGER tr AFTER INSERT ON a BEGIN DELETE FROM b; END"_s,
            false, kNone);
        row("empty", QString(), false, kNone);
        row("whitespace", u"  \n\t"_s, false, kNone);
        row("explain", u"EXPLAIN DELETE FROM t"_s, false, kNone);
        row("explain-query-plan", u"EXPLAIN QUERY PLAN DELETE FROM t"_s, false, kNone);
        row("explain-analyze-where", u"EXPLAIN ANALYZE DELETE FROM t WHERE a"_s, false, kNone);
        row("replace-into", u"REPLACE INTO t VALUES (1)"_s, true, kNone);
        row("insert", u"INSERT INTO t VALUES (1)"_s, true, kNone);
        row("paren-start", u"(DELETE FROM t)"_s, false, kNone);
        row("unterminated-comment", u"/* DELETE FROM t"_s, false, kNone);
        row("unterminated-string", u"SELECT 'abc DELETE FROM t"_s, false, kNone);
        row("where-after-block-comment", u"DELETE FROM t /* x */ WHERE a"_s, false, kNone);
        row("where-after-line-comment", u"DELETE FROM t -- x\nWHERE a"_s, false, kNone);
        row("update-where-subquery", u"UPDATE t SET a = (SELECT 1) WHERE id = (SELECT 2)"_s, false,
            kNone);
    }

    void check()
    {
        QFETCH(QString, sql);
        QFETCH(bool, production);
        QFETCH(int, expected);
        QCOMPARE(static_cast<int>(SqlSafety::check(sql, production)), expected);
    }

    void perf()
    {
        const QString sql = u"UPDATE accounts SET balance = balance + 1, note = 'x' "
                            "WHERE id IN (SELECT id FROM other WHERE a > 1) -- c"_s;
        QVERIFY(sql.size() >= 100);
        QElapsedTimer timer;
        timer.start();
        int flagged = 0;
        for (int i = 0; i < 10000; ++i) {
            flagged += SqlSafety::check(sql, true) != SqlRisk::None ? 1 : 0;
        }
        const qint64 ms = timer.elapsed();
        qInfo() << "10000 checks:" << ms << "ms";
        QCOMPARE(flagged, 0);
        QVERIFY2(ms < 500, "perf budget (generous for debug builds)");
    }
};

QTEST_GUILESS_MAIN(TstSqlSafety)
#include "tst_sql_safety.moc"
