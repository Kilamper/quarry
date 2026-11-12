#include <quarry/provider_registry.h>

#include <QtTest>

using namespace quarry;

class TstProviderRegistry : public QObject
{
    Q_OBJECT

private slots:
    void scanListsWithoutLoading();
    void providerLoadsLazilyAndCaches();
    void incompatibleApiIsRejected();
    void unknownIdFails();
    void missingDirectoryIsEmpty();
};

void TstProviderRegistry::scanListsWithoutLoading()
{
    ProviderRegistry registry;
    registry.scan(QStringLiteral(QUARRY_TEST_PLUGIN_DIR));

    QCOMPARE(registry.available().size(), size_t(1));
    QCOMPARE(registry.available().front().id, QStringLiteral("dummy"));
    QCOMPARE(registry.available().front().displayName, QStringLiteral("Dummy"));
    QCOMPARE(registry.available().front().version, QVersionNumber(0, 1, 0));
    QVERIFY(!registry.isLoaded(QStringLiteral("dummy")));
}

void TstProviderRegistry::providerLoadsLazilyAndCaches()
{
    ProviderRegistry registry;
    registry.scan(QStringLiteral(QUARRY_TEST_PLUGIN_DIR));

    QString error;
    IDatabaseProvider *p = registry.provider(QStringLiteral("dummy"), &error);
    QVERIFY2(p, qPrintable(error));
    QCOMPARE(p->info().id, QStringLiteral("dummy"));
    QVERIFY(registry.isLoaded(QStringLiteral("dummy")));
    QCOMPARE(registry.provider(QStringLiteral("dummy")), p);
}

void TstProviderRegistry::incompatibleApiIsRejected()
{
    ProviderRegistry registry;
    registry.scan(QStringLiteral(QUARRY_TEST_PLUGIN_DIR));

    QCOMPARE(registry.rejected().size(), size_t(1));
    QVERIFY(registry.rejected().front().reason.contains(QStringLiteral("API version"),
                                                        Qt::CaseInsensitive));

    QString error;
    QVERIFY(registry.provider(QStringLiteral("future"), &error) == nullptr);
    QVERIFY(!error.isEmpty());
}

void TstProviderRegistry::unknownIdFails()
{
    ProviderRegistry registry;
    registry.scan(QStringLiteral(QUARRY_TEST_PLUGIN_DIR));

    QString error;
    QVERIFY(registry.provider(QStringLiteral("nope"), &error) == nullptr);
    QVERIFY(!error.isEmpty());
}

void TstProviderRegistry::missingDirectoryIsEmpty()
{
    ProviderRegistry registry;
    registry.scan(QStringLiteral(QUARRY_TEST_PLUGIN_DIR "/does-not-exist"));
    QVERIFY(registry.available().empty());
    QVERIFY(registry.rejected().empty());
}

QTEST_GUILESS_MAIN(TstProviderRegistry)
#include "tst_provider_registry.moc"
