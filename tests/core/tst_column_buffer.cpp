#include <quarry/column_buffer.h>

#include <QtTest>

#include <limits>

using namespace quarry;

class TstColumnBuffer : public QObject
{
    Q_OBJECT

private slots:
    void typedInt64()
    {
        ColumnBuffer c;
        c.reserve(4);
        c.append(int64_t(1));
        c.append(std::numeric_limits<int64_t>::min());
        c.appendNull();
        c.append(int64_t(-3));
        QCOMPARE(c.size(), 4);
        QVERIFY(!c.isMixed());
        QCOMPARE(c.int64At(0), 1);
        QCOMPARE(c.int64At(1), std::numeric_limits<int64_t>::min());
        QVERIFY(c.isNull(2));
        QVERIFY(!c.isNull(3));
        QCOMPARE(c.int64At(3), -3);
        QCOMPARE(c.typeAt(0), ValueType::Int64);
        QCOMPARE(c.typeAt(2), ValueType::Null);
    }

    void typedDouble()
    {
        ColumnBuffer c;
        c.append(1.5);
        c.append(-0.25);
        QVERIFY(!c.isMixed());
        QCOMPARE(c.doubleAt(0), 1.5);
        QCOMPARE(c.doubleAt(1), -0.25);
        QCOMPARE(c.typeAt(1), ValueType::Double);
    }

    void typedBytesAndEmptyString()
    {
        ColumnBuffer c;
        QVERIFY(c.appendBytes("hello", ValueType::Text));
        c.appendBytes("", ValueType::Text);
        c.appendNull();
        c.appendBytes("wörld", ValueType::Text);
        QVERIFY(!c.isMixed());
        QCOMPARE(c.bytesAt(0), QByteArrayView("hello"));
        QCOMPARE(c.bytesAt(1).size(), 0);
        QVERIFY(!c.isNull(1));
        QVERIFY(c.isNull(2));
        QCOMPARE(c.bytesAt(3), QByteArrayView("wörld"));
        QCOMPARE(c.typeAt(3), ValueType::Text);
    }

    void boolColumn()
    {
        ColumnBuffer c;
        c.appendBool(true);
        c.appendBool(false);
        QVERIFY(!c.isMixed());
        QCOMPARE(c.typeAt(0), ValueType::Bool);
        QCOMPARE(c.int64At(0), 1);
        QCOMPARE(c.int64At(1), 0);
    }

    void leadingNullsThenTyped()
    {
        ColumnBuffer c;
        c.appendNull();
        c.appendNull();
        c.append(int64_t(9));
        QVERIFY(!c.isMixed());
        QVERIFY(c.isNull(0) && c.isNull(1));
        QCOMPARE(c.int64At(2), 9);
        QCOMPARE(c.typeAt(2), ValueType::Int64);
    }

    void nullBitmapAcrossWords()
    {
        ColumnBuffer c;
        for (int i = 0; i < 200; ++i) {
            if (i % 7 == 0)
                c.appendNull();
            else
                c.append(int64_t(i));
        }
        for (int i = 0; i < 200; ++i) {
            QCOMPARE(c.isNull(i), i % 7 == 0);
            if (i % 7 != 0)
                QCOMPARE(c.int64At(i), i);
        }
    }

    void mixedPromotion()
    {
        ColumnBuffer c;
        c.append(int64_t(10));
        c.append(int64_t(20));
        QVERIFY(!c.isMixed());
        c.appendBytes("text", ValueType::Text);
        QVERIFY(c.isMixed());
        c.appendBytes(QByteArray("\x00\x01\xff", 3), ValueType::Bytes);
        c.appendNull();
        c.append(2.5);

        QCOMPARE(c.size(), 6);
        QCOMPARE(c.typeAt(0), ValueType::Int64);
        QCOMPARE(c.int64At(0), 10);
        QCOMPARE(c.typeAt(1), ValueType::Int64);
        QCOMPARE(c.int64At(1), 20);
        QCOMPARE(c.typeAt(2), ValueType::Text);
        QCOMPARE(c.bytesAt(2), QByteArrayView("text"));
        QCOMPARE(c.typeAt(3), ValueType::Bytes);
        QCOMPARE(c.bytesAt(3), QByteArrayView(QByteArray("\x00\x01\xff", 3)));
        QCOMPARE(c.typeAt(4), ValueType::Null);
        QVERIFY(c.isNull(4));
        QCOMPARE(c.typeAt(5), ValueType::Double);
        QCOMPARE(c.doubleAt(5), 2.5);
    }

    void mixedPromotionFromBytesWithNulls()
    {
        ColumnBuffer c;
        c.appendNull();
        c.appendBytes("a", ValueType::Text);
        c.append(int64_t(5));
        QVERIFY(c.isMixed());
        QCOMPARE(c.typeAt(0), ValueType::Null);
        QCOMPARE(c.typeAt(1), ValueType::Text);
        QCOMPARE(c.bytesAt(1), QByteArrayView("a"));
        QCOMPARE(c.typeAt(2), ValueType::Int64);
        QCOMPARE(c.int64At(2), 5);
    }

    void bytesViewIsStableWithoutAppends()
    {
        ColumnBuffer c;
        c.reserve(1000);
        for (int i = 0; i < 1000; ++i)
            c.appendBytes(QByteArray::number(i), ValueType::Text);
        const QByteArrayView v = c.bytesAt(500);
        const char *p = v.data();
        QCOMPARE(c.bytesAt(500).data(), p); // same arena memory, no copy
        QCOMPARE(v, QByteArrayView("500"));
        QCOMPARE(c.bytesAt(999), QByteArrayView("999"));
    }
};

QTEST_GUILESS_MAIN(TstColumnBuffer)
#include "tst_column_buffer.moc"
