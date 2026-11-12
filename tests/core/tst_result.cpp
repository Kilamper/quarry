#include <quarry/result.h>

#include <QtTest>

#include <memory>

using namespace quarry;

namespace {

struct Base
{
    virtual ~Base() = default;
    virtual int id() const { return 0; }
};

struct Derived : Base
{
    int id() const override { return 7; }
};

Result<int> half(int n)
{
    if (n % 2 != 0)
        return fail({ ErrorKind::Syntax, QStringLiteral("odd"), QStringLiteral("d"), {}, 3 });
    return n / 2;
}

Result<std::unique_ptr<Base>> makeDerived()
{
    return std::make_unique<Derived>();
}

} // namespace

class TstResult : public QObject
{
    Q_OBJECT

private slots:
    void valuePath()
    {
        auto r = half(10);
        QVERIFY(r.has_value());
        QVERIFY(r);
        QCOMPARE(r.value(), 5);
        QCOMPARE(*r, 5);
    }

    void errorPath()
    {
        const auto r = half(3);
        QVERIFY(!r.has_value());
        QVERIFY(!r);
        QCOMPARE(r.error().kind, ErrorKind::Syntax);
        QCOMPARE(r.error().message, QStringLiteral("odd"));
        QCOMPARE(r.error().detail, QStringLiteral("d"));
        QCOMPARE(r.error().position.value_or(-1), 3);
    }

    void voidResult()
    {
        const Result<void> ok;
        QVERIFY(ok);
        ok.value();
        const Result<void> bad = fail(Error { ErrorKind::Driver, QStringLiteral("m"), {}, {}, {} });
        QVERIFY(!bad.has_value());
        QCOMPARE(bad.error().kind, ErrorKind::Driver);
    }

    void moveOnlyValue()
    {
        Result<std::unique_ptr<int>> r(std::make_unique<int>(42));
        QVERIFY(r);
        QCOMPARE(**r, 42);
        std::unique_ptr<int> taken = std::move(r).value();
        QCOMPARE(*taken, 42);
    }

    void derivedConvertsToBasePointer()
    {
        auto r = makeDerived();
        QVERIFY(r);
        QCOMPARE((*r)->id(), 7);
    }

    void arrowOperator()
    {
        Result<QString> r(QStringLiteral("abc"));
        QCOMPARE(r->size(), 3);
    }

    void movedError()
    {
        Result<int> r = fail(Error { ErrorKind::Timeout, QStringLiteral("slow"), {}, {}, {} });
        const Error e = std::move(r).error();
        QCOMPARE(e.message, QStringLiteral("slow"));
    }
};

QTEST_GUILESS_MAIN(TstResult)
#include "tst_result.moc"
