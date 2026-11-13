#include "provider_conformance.h"

#include <QtTest>

#include <atomic>
#include <future>

namespace quarry::conformance {

namespace {

// Qt Test macros return void, so the value-returning helpers record the failure by hand.
void recordFailure(const QString &text, int line)
{
    QTest::qFail(qPrintable(text), __FILE__, line);
}

bool containsName(const std::vector<SchemaNode> &nodes, ObjectKind kind, const QString &name)
{
    return std::any_of(nodes.begin(), nodes.end(), [&](const SchemaNode &node) {
        return node.path.kind == kind && node.name == name;
    });
}

// Depth-first walk of the whole tree below `parent`, appending every descendant to `out`.
bool collect(IConnection &connection, const ObjectPath &parent, int depth,
             std::vector<SchemaNode> &out)
{
    if (depth > 6)
        return false;
    auto children = connection.children(parent, {});
    if (!children)
        return false;
    for (const SchemaNode &node : children.value()) {
        out.push_back(node);
        if (node.hasChildren && !collect(connection, node.path, depth + 1, out))
            return false;
    }
    return true;
}

} // namespace

void ProviderConformanceTest::init()
{
    m_fixture = createFixture();
}

void ProviderConformanceTest::cleanup()
{
    m_fixture.reset();
}

std::unique_ptr<IConnection> ProviderConformanceTest::openConnection(bool withSchema)
{
    auto connection = fixture().provider().createConnection(fixture().config(), {});
    if (!connection) {
        recordFailure(QStringLiteral("createConnection returned null"), __LINE__);
        return nullptr;
    }
    auto info = connection->open({});
    if (!info) {
        recordFailure(info.error().message + QStringLiteral(" | ") + info.error().detail, __LINE__);
        return nullptr;
    }
    if (withSchema) {
        const QString script = fixture().sampleSchemaSql();
        for (const StatementRange &range : fixture().provider().dialect().splitStatements(script)) {
            const auto d = drain(*connection, script.mid(range.begin, range.end - range.begin));
            if (!d.ok) {
                recordFailure(QStringLiteral("schema setup failed: ") + d.errorText(), __LINE__);
                return nullptr;
            }
        }
    }
    return connection;
}

ProviderConformanceTest::Drained ProviderConformanceTest::drain(IConnection &connection,
                                                                const QString &sql,
                                                                const QVariantList &parameters)
{
    Drained out;
    auto stream = connection.execute(Statement { sql, parameters, 500 }, {});
    if (!stream) {
        out.error = stream.error();
        return out;
    }
    IResultStream &s = *stream.value();
    out.columns = s.columns();
    for (int guard = 0; !s.atEnd() && guard < 1'000'000; ++guard) {
        auto batch = s.fetch(500, {});
        if (!batch) {
            out.error = batch.error();
            return out;
        }
        out.rows += batch.value().rowCount;
        out.batches.push_back(std::move(batch).value());
    }
    out.affected = s.affectedRows();
    out.ok = true;
    return out;
}

void ProviderConformanceTest::openCloseServerInfo()
{
    auto connection = fixture().provider().createConnection(fixture().config(), {});
    QVERIFY(connection);
    QVERIFY(!connection->isOpen());
    auto info = connection->open({});
    QVERIFY2(info, qPrintable(info ? QString() : info.error().detail));
    QVERIFY(!info.value().product.isEmpty());
    QVERIFY(!info.value().version.isEmpty());
    QVERIFY(connection->isOpen());
    connection->close();
    QVERIFY(!connection->isOpen());
    QVERIFY(connection->open({}));
    QVERIFY(connection->isOpen());
    const auto fields = fixture().provider().connectionFields();
    QVERIFY(!fields.empty());
    for (const auto &field : fields)
        QVERIFY(!field.key.isEmpty() && !field.label.isEmpty());
}

void ProviderConformanceTest::emptyResultKeepsColumns()
{
    auto connection = openConnection();
    QVERIFY(connection);
    const auto d = drain(*connection, QStringLiteral("SELECT id, title FROM books WHERE id < 0"));
    QVERIFY2(d.ok, qPrintable(d.errorText()));
    QCOMPARE(d.columns.size(), size_t(2));
    QCOMPARE(d.columns[0].name, QStringLiteral("id"));
    QCOMPARE(d.columns[1].name, QStringLiteral("title"));
    QCOMPARE(d.rows, 0);
}

void ProviderConformanceTest::typesRoundTripWithNulls()
{
    auto connection = openConnection();
    QVERIFY(connection);
    QVERIFY(drain(*connection,
                  QStringLiteral("INSERT INTO authors(id, name) VALUES(%1, %2)")
                      .arg(fixture().placeholder(1), fixture().placeholder(2)),
                  { 1, QStringLiteral("Ann") })
                .ok);

    const QString insert
        = QStringLiteral("INSERT INTO books(id, title, author_id, price, cover) "
                         "VALUES(%1, %2, %3, %4, %5)")
              .arg(fixture().placeholder(1), fixture().placeholder(2), fixture().placeholder(3),
                   fixture().placeholder(4), fixture().placeholder(5));
    const qint64 bigId = 9'007'199'254'740'993LL; // beyond 2^53: must not go through a double
    const QString title = QStringLiteral("Ünï cødé ✓ 日本 😀");
    const QByteArray cover("\x00\xff\x10 z", 5);
    auto first = drain(*connection, insert,
                       { QVariant::fromValue(bigId), title, 1, 12.5, QVariant(cover) });
    QVERIFY2(first.ok, qPrintable(first.errorText()));
    auto second
        = drain(*connection, insert, { 2, QStringLiteral("plain"), 1, QVariant(), QVariant() });
    QVERIFY2(second.ok, qPrintable(second.errorText()));

    const auto d = drain(*connection,
                         QStringLiteral("SELECT id, title, price, cover FROM books ORDER BY id"));
    QVERIFY2(d.ok, qPrintable(d.errorText()));
    QCOMPARE(d.rows, 2);
    QCOMPARE(d.columns.size(), size_t(4));
    const ResultBatch &b = d.batches.front();
    QCOMPARE(b.columns[0].typeAt(0), ValueType::Int64);
    QCOMPARE(b.columns[0].int64At(0), 2);
    QCOMPARE(b.columns[0].int64At(1), bigId);
    QCOMPARE(b.columns[1].typeAt(1), ValueType::Text);
    QCOMPARE(QString::fromUtf8(b.columns[1].bytesAt(1)), title);
    QCOMPARE(b.columns[2].typeAt(1), ValueType::Double);
    QCOMPARE(b.columns[2].doubleAt(1), 12.5);
    QCOMPARE(b.columns[3].typeAt(1), ValueType::Bytes);
    QCOMPARE(QByteArray(b.columns[3].bytesAt(1)), cover);
    QVERIFY(b.columns[2].isNull(0));
    QVERIFY(b.columns[3].isNull(0));
    QCOMPARE(b.columns[2].typeAt(0), ValueType::Null);

    // Literal values of every kind, including NULL, without any table.
    const auto lit
        = drain(*connection, QStringLiteral("SELECT 1 AS a, 'x' AS b, 2.5 AS c, NULL AS d"));
    QVERIFY2(lit.ok, qPrintable(lit.errorText()));
    QCOMPARE(lit.columns.size(), size_t(4));
    const ResultBatch &l = lit.batches.front();
    QCOMPARE(l.columns[0].typeAt(0), ValueType::Int64);
    QCOMPARE(l.columns[1].typeAt(0), ValueType::Text);
    QCOMPARE(l.columns[2].typeAt(0), ValueType::Double);
    QVERIFY(l.columns[3].isNull(0));
}

void ProviderConformanceTest::parametersAreBoundNotConcatenated()
{
    auto connection = openConnection();
    QVERIFY(connection);
    const QString hostile = QStringLiteral("Robert'); DROP TABLE authors;--");
    QVERIFY(drain(*connection,
                  QStringLiteral("INSERT INTO authors(id, name) VALUES(%1, %2)")
                      .arg(fixture().placeholder(1), fixture().placeholder(2)),
                  { 7, hostile })
                .ok);
    const auto d = drain(*connection,
                         QStringLiteral("SELECT id, name FROM authors WHERE name = %1")
                             .arg(fixture().placeholder(1)),
                         { hostile });
    QVERIFY2(d.ok, qPrintable(d.errorText()));
    QCOMPARE(d.rows, 1);
    QCOMPARE(d.batches.front().columns[0].int64At(0), 7);
    QCOMPARE(QString::fromUtf8(d.batches.front().columns[1].bytesAt(0)), hostile);

    // Wrong number of parameters is an error, not a silent NULL.
    const auto bad = drain(*connection, QStringLiteral("SELECT %1").arg(fixture().placeholder(1)));
    QVERIFY(!bad.ok);
}

void ProviderConformanceTest::streamsInBatches()
{
    auto connection = openConnection(false);
    QVERIFY(connection);
    auto stream = connection->execute(Statement { fixture().seriesSql(1050), {}, 100 }, {});
    QVERIFY2(stream, qPrintable(stream ? QString() : stream.error().detail));
    IResultStream &s = *stream.value();
    QCOMPARE(s.columns().size(), size_t(1));

    qint64 total = 0;
    std::vector<qsizetype> sizes;
    for (int guard = 0; !s.atEnd() && guard < 50; ++guard) {
        auto batch = s.fetch(100, {});
        QVERIFY2(batch, qPrintable(batch ? QString() : batch.error().detail));
        const ResultBatch &b = batch.value();
        QVERIFY(b.rowCount <= 100);
        QCOMPARE(b.columns.size(), size_t(1));
        QCOMPARE(b.columns[0].size(), b.rowCount);
        for (qsizetype i = 0; i < b.rowCount; ++i)
            QCOMPARE(b.columns[0].int64At(i), total + i + 1);
        total += b.rowCount;
        sizes.push_back(b.rowCount);
        if (sizes.size() == 1)
            QVERIFY(!s.atEnd()); // 950 rows are still pending
    }
    QVERIFY(s.atEnd());
    QCOMPARE(total, 1050);
    QVERIFY(sizes.size() >= 11);
    for (size_t i = 0; i + 1 < sizes.size(); ++i)
        QVERIFY2(sizes[i] == 100 || sizes[i] == 0, "fetchSize not honored");

    // Fetching after the end keeps returning an empty batch.
    auto after = s.fetch(100, {});
    QVERIFY(after);
    QCOMPARE(after.value().rowCount, 0);
    QVERIFY(s.atEnd());
}

void ProviderConformanceTest::affectedRowsForUpdateAndDelete()
{
    auto connection = openConnection();
    QVERIFY(connection);
    for (int id = 1; id <= 3; ++id) {
        QVERIFY(drain(*connection,
                      QStringLiteral("INSERT INTO authors(id, name) VALUES(%1, %2)")
                          .arg(fixture().placeholder(1), fixture().placeholder(2)),
                      { id, QStringLiteral("author %1").arg(id) })
                    .ok);
    }
    auto update
        = drain(*connection, QStringLiteral("UPDATE authors SET name = name || '!' WHERE id <= 2"));
    QVERIFY2(update.ok, qPrintable(update.errorText()));
    QCOMPARE(update.affected.value_or(-1), 2);
    QVERIFY(update.columns.empty());

    auto none = drain(*connection, QStringLiteral("UPDATE authors SET name = 'x' WHERE id = 99"));
    QVERIFY(none.ok);
    QCOMPARE(none.affected.value_or(-1), 0);

    auto del = drain(*connection, QStringLiteral("DELETE FROM authors WHERE id = 3"));
    QVERIFY2(del.ok, qPrintable(del.errorText()));
    QCOMPARE(del.affected.value_or(-1), 1);
}

void ProviderConformanceTest::syntaxErrorHasPosition()
{
    auto connection = openConnection();
    QVERIFY(connection);
    const QString sql = QStringLiteral("SELEC 1");
    auto stream = connection->execute(Statement { sql, {}, 500 }, {});
    QVERIFY(!stream);
    const Error error = std::move(stream).error();
    QCOMPARE(error.kind, ErrorKind::Syntax);
    QVERIFY(!error.message.isEmpty());
    QVERIFY(!error.detail.isEmpty());
    const int position = error.position.value_or(-1);
    QVERIFY2(position >= 0 && position <= sql.size(), "missing or out-of-range position");
    // The connection is still usable after an error.
    QVERIFY(drain(*connection, QStringLiteral("SELECT 1")).ok);
}

void ProviderConformanceTest::constraintViolationKind()
{
    auto connection = openConnection();
    QVERIFY(connection);
    const QString insert = QStringLiteral("INSERT INTO authors(id, name) VALUES(%1, %2)")
                               .arg(fixture().placeholder(1), fixture().placeholder(2));
    QVERIFY(drain(*connection, insert, { 1, QStringLiteral("Ann") }).ok);
    const auto duplicate = drain(*connection, insert, { 1, QStringLiteral("Bob") });
    QVERIFY(!duplicate.ok);
    QCOMPARE(duplicate.error.kind, ErrorKind::Constraint);
    QVERIFY(!duplicate.error.message.isEmpty());
    const auto notNull = drain(*connection, insert, { 2, QVariant() });
    QVERIFY(!notNull.ok);
    QCOMPARE(notNull.error.kind, ErrorKind::Constraint);
    const auto foreignKey = drain(*connection,
                                  QStringLiteral("INSERT INTO books(id, title, author_id) "
                                                 "VALUES(1, 't', 999)"));
    QVERIFY(!foreignKey.ok);
    QCOMPARE(foreignKey.error.kind, ErrorKind::Constraint);
}

void ProviderConformanceTest::missingTableIsNotFound()
{
    auto connection = openConnection();
    QVERIFY(connection);
    const auto d = drain(*connection, QStringLiteral("SELECT * FROM no_such_table"));
    QVERIFY(!d.ok);
    QCOMPARE(d.error.kind, ErrorKind::NotFound);
}

void ProviderConformanceTest::cancelLongQueryIsFastAndConnectionSurvives()
{
    auto connection = openConnection(false);
    QVERIFY(connection);
    std::atomic<bool> started { false };
    const QString slow = fixture().slowQuerySql();
    auto worker = std::async(std::launch::async, [&]() -> std::optional<ErrorKind> {
        started = true;
        auto stream = connection->execute(Statement { slow, {}, 500 }, {});
        if (!stream)
            return stream.error().kind;
        auto batch = stream.value()->fetch(500, {});
        if (!batch)
            return batch.error().kind;
        return std::nullopt;
    });
    QTRY_VERIFY_WITH_TIMEOUT(started.load(), 5000);
    QThread::msleep(150); // let it get properly stuck inside the engine

    QElapsedTimer clock;
    clock.start();
    connection->requestCancel();
    QVERIFY2(worker.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
             "the query did not stop");
    const qint64 elapsed = clock.elapsed();
    const auto kind = worker.get();
    QVERIFY2(kind.has_value(), "the query finished instead of being cancelled");
    QCOMPARE(kind.value_or(ErrorKind::Internal), ErrorKind::Cancelled);
    QVERIFY2(elapsed < 200, qPrintable(QStringLiteral("cancel took %1 ms").arg(elapsed)));

    const auto d = drain(*connection, QStringLiteral("SELECT 1"));
    QVERIFY2(d.ok, qPrintable(d.errorText()));
    QCOMPARE(d.rows, 1);
}

void ProviderConformanceTest::staleCancelWhileIdleIsIgnored()
{
    auto connection = openConnection(false);
    QVERIFY(connection);
    connection->requestCancel(); // nothing is running: must not poison the next statement
    const auto d = drain(*connection, QStringLiteral("SELECT 1"));
    QVERIFY2(d.ok, qPrintable(d.errorText()));
    QCOMPARE(d.rows, 1);
}

void ProviderConformanceTest::staleCancelBetweenFetchesIsIgnored()
{
    auto connection = openConnection(false);
    QVERIFY(connection);
    auto stream = connection->execute(Statement { fixture().seriesSql(1000), {}, 10 }, {});
    QVERIFY(stream);
    auto first = stream.value()->fetch(10, {});
    QVERIFY(first);
    QCOMPARE(first.value().rowCount, 10);
    connection->requestCancel(); // stream is idle between fetches
    auto second = stream.value()->fetch(10, {});
    QVERIFY2(second, qPrintable(second ? QString() : second.error().detail));
    QCOMPARE(second.value().rowCount, 10);
    QCOMPARE(second.value().columns[0].int64At(0), 11);
}

void ProviderConformanceTest::cancelledTokenIsHonored()
{
    auto connection = openConnection(false);
    QVERIFY(connection);
    CancellationSource source;
    source.cancel();
    auto rejected
        = connection->execute(Statement { QStringLiteral("SELECT 1"), {}, 500 }, source.token());
    QVERIFY(!rejected);
    QCOMPARE(std::move(rejected).error().kind, ErrorKind::Cancelled);

    {
        auto stream = connection->execute(Statement { fixture().seriesSql(1000), {}, 10 }, {});
        QVERIFY(stream);
        auto batch = stream.value()->fetch(10, source.token());
        QVERIFY(!batch);
        QCOMPARE(std::move(batch).error().kind, ErrorKind::Cancelled);
    }
    // And the connection is fine afterwards.
    QVERIFY(drain(*connection, QStringLiteral("SELECT 1")).ok);
}

void ProviderConformanceTest::introspectionTree()
{
    auto connection = openConnection();
    QVERIFY(connection);
    std::vector<SchemaNode> all;
    QVERIFY(collect(*connection, ObjectPath { {}, ObjectKind::Database }, 0, all));

    auto find = [&](const ObjectPath &target) {
        return std::find_if(all.begin(), all.end(), [&](const SchemaNode &node) {
            return node.path.kind == target.kind && node.path.parts == target.parts;
        });
    };
    const auto books = find(fixture().booksPath());
    QVERIFY2(books != all.end(), "books table not reachable from the root");
    QVERIFY(books->hasChildren);
    const auto view = find(fixture().viewPath());
    QVERIFY2(view != all.end(), "view not reachable from the root");
    QCOMPARE(view->path.kind, ObjectKind::View);

    std::vector<SchemaNode> tableNodes;
    QVERIFY(collect(*connection, fixture().booksPath(), 0, tableNodes));
    for (const QString &column :
         { QStringLiteral("id"), QStringLiteral("title"), QStringLiteral("author_id"),
           QStringLiteral("price"), QStringLiteral("cover") })
        QVERIFY2(containsName(tableNodes, ObjectKind::Column, column), qPrintable(column));
    QVERIFY(containsName(tableNodes, ObjectKind::Index, QStringLiteral("idx_books_title")));
    const auto fks = std::count_if(tableNodes.begin(), tableNodes.end(), [](const SchemaNode &n) {
        return n.path.kind == ObjectKind::ForeignKey;
    });
    QCOMPARE(fks, 1);
    for (const SchemaNode &node : tableNodes) {
        if (node.path.kind == ObjectKind::Column) {
            QVERIFY(!node.hasChildren);
            if (node.name == QLatin1String("title"))
                QVERIFY(!node.detail.isEmpty());
        }
    }

    std::vector<SchemaNode> viewNodes;
    QVERIFY(collect(*connection, fixture().viewPath(), 0, viewNodes));
    QVERIFY(containsName(viewNodes, ObjectKind::Column, QStringLiteral("title")));
}

void ProviderConformanceTest::describeTable()
{
    auto connection = openConnection();
    QVERIFY(connection);
    auto details = connection->describe(fixture().booksPath(), {});
    QVERIFY2(details, qPrintable(details ? QString() : details.error().detail));
    const TableDetails &t = details.value();

    QStringList names;
    for (const ColumnDef &c : t.columns)
        names << c.name;
    QCOMPARE(
        names,
        (QStringList { QStringLiteral("id"), QStringLiteral("title"), QStringLiteral("author_id"),
                       QStringLiteral("price"), QStringLiteral("cover") }));
    QCOMPARE(t.columns[0].type, ValueType::Int64);
    QCOMPARE(t.columns[1].type, ValueType::Text);
    QVERIFY(!t.columns[1].nullable);
    QVERIFY(t.columns[3].nullable);
    QVERIFY(!t.columns[1].nativeType.isEmpty());

    QVERIFY(t.primaryKey.has_value());
    QCOMPARE(t.primaryKey.value_or(QStringList()), QStringList { QStringLiteral("id") });

    const auto index = std::find_if(t.indexes.begin(), t.indexes.end(), [](const IndexDef &i) {
        return i.name == QLatin1String("idx_books_title");
    });
    QVERIFY(index != t.indexes.end());
    QCOMPARE(index->columns, QStringList { QStringLiteral("title") });
    QVERIFY(!index->unique);

    QCOMPARE(t.foreignKeys.size(), size_t(1));
    QCOMPARE(t.foreignKeys[0].columns, QStringList { QStringLiteral("author_id") });
    QCOMPARE(t.foreignKeys[0].referencedColumns, QStringList { QStringLiteral("id") });
    QVERIFY(!t.foreignKeys[0].referencedTable.parts.isEmpty());
    QCOMPARE(t.foreignKeys[0].referencedTable.parts.last(), QStringLiteral("authors"));

    auto missing = connection->describe(
        ObjectPath { fixture().booksPath().parts.mid(0, fixture().booksPath().parts.size() - 1)
                         + QStringList { QStringLiteral("no_such_table") },
                     ObjectKind::Table },
        {});
    QVERIFY(!missing);
    QCOMPARE(std::move(missing).error().kind, ErrorKind::NotFound);
}

void ProviderConformanceTest::quotingWorksInsideStatements()
{
    auto connection = openConnection(false);
    QVERIFY(connection);
    const ISqlDialect &dialect = fixture().provider().dialect();
    const QString table = QStringLiteral("we\"ird; name'x");
    const QString create
        = QStringLiteral("CREATE TABLE %1 (a INTEGER)").arg(dialect.quoteIdentifier(table));
    QVERIFY2(drain(*connection, create).ok, qPrintable(create));
    QVERIFY(drain(*connection,
                  QStringLiteral("INSERT INTO %1 VALUES (5)").arg(dialect.quoteIdentifier(table)))
                .ok);
    const auto count = drain(
        *connection, QStringLiteral("SELECT a FROM %1").arg(dialect.quoteIdentifier(table)));
    QVERIFY2(count.ok, qPrintable(count.errorText()));
    QCOMPARE(count.rows, 1);

    const QString text = QStringLiteral("it's; \"quoted\" -- not a comment");
    const auto literal
        = drain(*connection, QStringLiteral("SELECT %1").arg(dialect.quoteLiteral(text)));
    QVERIFY2(literal.ok, qPrintable(literal.errorText()));
    QCOMPARE(QString::fromUtf8(literal.batches.front().columns[0].bytesAt(0)), text);

    QVERIFY(!dialect.highlightingDefinition().isEmpty());
    QVERIFY(!dialect.keywords().isEmpty());
    QVERIFY(dialect.limitClause(10, 0).contains(QLatin1String("10")));
    QVERIFY(dialect.limitClause(10, 20).contains(QLatin1String("20")));
}

void ProviderConformanceTest::splitStatements()
{
    std::vector<SplitCase> cases {
        { QStringLiteral("SELECT 1; SELECT 2;"),
          { QStringLiteral("SELECT 1"), QStringLiteral("SELECT 2") } },
        { QStringLiteral("  SELECT 1  ;\n\n;; SELECT 2"),
          { QStringLiteral("SELECT 1"), QStringLiteral("SELECT 2") } },
        { QStringLiteral("SELECT 'a;b'; SELECT \"c;d\""),
          { QStringLiteral("SELECT 'a;b'"), QStringLiteral("SELECT \"c;d\"") } },
        { QStringLiteral("SELECT 1 -- ; not a split\n; SELECT 2"),
          { QStringLiteral("SELECT 1 -- ; not a split"), QStringLiteral("SELECT 2") } },
        { QStringLiteral("/* ; */ SELECT 1; /* x */"), { QStringLiteral("/* ; */ SELECT 1") } },
        { QStringLiteral("SELECT 'it''s;'; SELECT 2"),
          { QStringLiteral("SELECT 'it''s;'"), QStringLiteral("SELECT 2") } },
        { QStringLiteral("SELECT 'é;ü'; SELECT '日本;'"),
          { QStringLiteral("SELECT 'é;ü'"), QStringLiteral("SELECT '日本;'") } },
        { QString(), {} },
        { QStringLiteral("  ;  ; "), {} },
        { QStringLiteral("-- only a comment"), {} },
    };
    for (SplitCase &extra : fixture().extraSplitCases())
        cases.push_back(std::move(extra));

    const ISqlDialect &dialect = fixture().provider().dialect();
    for (const SplitCase &c : cases) {
        const auto ranges = dialect.splitStatements(c.script);
        QStringList actual;
        qsizetype previousEnd = 0;
        for (const StatementRange &r : ranges) {
            QVERIFY2(r.begin >= previousEnd && r.begin < r.end && r.end <= c.script.size(),
                     qPrintable(c.script));
            previousEnd = r.end;
            actual << c.script.mid(r.begin, r.end - r.begin);
        }
        QVERIFY2(actual == c.expected,
                 qPrintable(QStringLiteral("script: %1\nactual: %2")
                                .arg(c.script, actual.join(QLatin1String(" || ")))));
    }
}

} // namespace quarry::conformance
