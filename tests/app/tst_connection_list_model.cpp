#include "ui/connection_form_model.h"
#include "ui/connection_list_model.h"

#include <quarry/provider_registry.h>

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace quarry;
using quarry::ui::ConnectionFormModel;
using quarry::ui::ConnectionListModel;

namespace {

ConnectionProfile profileNamed(const QString &name, const QString &file = {})
{
    ConnectionProfile p;
    p.name = name;
    p.providerId = QStringLiteral("sqlite");
    p.values = { { QStringLiteral("file"), file.isEmpty() ? name + QStringLiteral(".db") : file } };
    return p;
}

QStringList names(const ConnectionListModel &m)
{
    QStringList out;
    for (int i = 0; i < m.rowCount(); ++i)
        out << m.data(m.index(i), ConnectionListModel::NameRole).toString();
    return out;
}

} // namespace

class TstConnectionListModel : public QObject
{
    Q_OBJECT

private slots:
    void startsLoadingThenFills();
    void rolesDescribeTheConnection();
    void keepsNameOrderAndMovesOnRename();
    void removeAndIndexOf();
    void noRoleCarriesASecret();
    void loadFailureIsReported();
    void formModelFieldsAndValidation();
};

void TstConnectionListModel::startsLoadingThenFills()
{
    QTemporaryDir dir;
    ConnectionListModel model(dir.filePath(QStringLiteral("q.db")));
    QAbstractItemModelTester tester(&model);
    QVERIFY(model.loading()); // the first frame must not claim "no connections"
    QCOMPARE(model.rowCount(), 0);

    QSignalSpy loading(&model, &ConnectionListModel::loadingChanged);
    model.start();
    QTRY_VERIFY_WITH_TIMEOUT(!model.loading(), 10000);
    QVERIFY(!loading.isEmpty());
    QVERIFY(model.lastError().isEmpty());
    QCOMPARE(model.count(), 0);

    // Another model on the same file sees what was saved: the load is real, not a stub.
    model.store()->save(profileNamed(QStringLiteral("alpha")));
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 1, 10000);
    ConnectionListModel again(dir.filePath(QStringLiteral("q.db")));
    QAbstractItemModelTester tester2(&again);
    again.start();
    QTRY_COMPARE_WITH_TIMEOUT(again.rowCount(), 1, 10000);
    QVERIFY(!again.loading());
}

void TstConnectionListModel::rolesDescribeTheConnection()
{
    QTemporaryDir dir;
    ProviderRegistry registry;
    registry.scan(QStringLiteral(QUARRY_PLUGIN_DIR));
    ConnectionListModel model(dir.filePath(QStringLiteral("q.db")));
    QAbstractItemModelTester tester(&model);
    model.setRegistry(&registry);

    ConnectionProfile p
        = profileNamed(QStringLiteral("Prod"), QStringLiteral("C:/data/shop.sqlite"));
    p.colorIndex = 3;
    p.production = true;
    p.passwordMode = PasswordMode::AskEveryTime;
    model.store()->save(p);
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 1, 10000);

    const QModelIndex i = model.index(0);
    QCOMPARE(model.data(i, ConnectionListModel::NameRole).toString(), QStringLiteral("Prod"));
    QCOMPARE(model.data(i, ConnectionListModel::ProviderIdRole).toString(),
             QStringLiteral("sqlite"));
    QCOMPARE(model.data(i, ConnectionListModel::ProviderNameRole).toString(),
             QStringLiteral("SQLite"));
    QCOMPARE(model.data(i, ConnectionListModel::SummaryRole).toString(),
             QStringLiteral("shop.sqlite"));
    QCOMPARE(model.data(i, ConnectionListModel::ColorIndexRole).toInt(), 3);
    QCOMPARE(model.data(i, ConnectionListModel::ColorNameRole).toString(),
             QStringLiteral("Purple"));
    QVERIFY(model.data(i, ConnectionListModel::ProductionRole).toBool());
    QCOMPARE(model.data(i, ConnectionListModel::PasswordModeRole).toInt(),
             static_cast<int>(PasswordMode::AskEveryTime));
    QVERIFY(!model.data(i, ConnectionListModel::IdRole).toString().isEmpty());
    QCOMPARE(model.get(0).value(QStringLiteral("name")).toString(), QStringLiteral("Prod"));

    ConnectionProfile server;
    server.values = { { QStringLiteral("host"), QStringLiteral("db.example.com") },
                      { QStringLiteral("port"), 5432 },
                      { QStringLiteral("database"), QStringLiteral("shop") } };
    QCOMPARE(ConnectionListModel::summaryOf(server), QStringLiteral("db.example.com:5432/shop"));
}

void TstConnectionListModel::keepsNameOrderAndMovesOnRename()
{
    QTemporaryDir dir;
    ConnectionListModel model(dir.filePath(QStringLiteral("q.db")));
    QAbstractItemModelTester tester(&model);
    for (const char *n : { "charlie", "Alpha", "bravo" })
        model.store()->save(profileNamed(QString::fromLatin1(n)));
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 3, 10000);
    QCOMPARE(names(model), (QStringList { "Alpha", "bravo", "charlie" }));

    auto bravo = model.profile(
        QUuid::fromString(model.get(1).value(QStringLiteral("connectionId")).toString()));
    if (!bravo.has_value())
        QFAIL("no profile");
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    bravo->production = true;
    model.store()->save(*bravo); // same name: updated in place
    QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 10000);
    QCOMPARE(names(model), (QStringList { "Alpha", "bravo", "charlie" }));

    bravo->name = QStringLiteral("zulu");
    model.store()->save(*bravo); // new name: moves to the end
    QTRY_COMPARE_WITH_TIMEOUT(names(model), (QStringList { "Alpha", "charlie", "zulu" }), 10000);
    QCOMPARE(model.count(), 3);
}

void TstConnectionListModel::removeAndIndexOf()
{
    QTemporaryDir dir;
    ConnectionListModel model(dir.filePath(QStringLiteral("q.db")));
    QAbstractItemModelTester tester(&model);
    model.store()->save(profileNamed(QStringLiteral("one")));
    model.store()->save(profileNamed(QStringLiteral("two")));
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 2, 10000);
    const QString id = model.get(1).value(QStringLiteral("connectionId")).toString();
    QCOMPARE(model.indexOf(id), 1);
    QCOMPARE(model.indexOf(QStringLiteral("nope")), -1);
    model.store()->remove(QUuid::fromString(id));
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 1, 10000);
    QCOMPARE(model.indexOf(id), -1);
}

void TstConnectionListModel::noRoleCarriesASecret()
{
    const QString secret = QStringLiteral("S3cr3t-ROLE-CHECK-9981");
    QTemporaryDir dir;
    ConnectionListModel model(dir.filePath(QStringLiteral("q.db")));
    QAbstractItemModelTester tester(&model);
    ConnectionProfile p = profileNamed(QStringLiteral("withsecrets"));
    p.values.insert(QStringLiteral("password"), secret);
    p.values.insert(QStringLiteral("token"), secret);
    model.store()->save(p, { QStringLiteral("token") });
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 1, 10000);
    const auto row = model.get(0);
    for (auto it = row.cbegin(); it != row.cend(); ++it)
        QVERIFY2(!it.value().toString().contains(secret), qPrintable(it.key()));
    const auto stored
        = model.profile(QUuid::fromString(row.value(QStringLiteral("connectionId")).toString()));
    if (!stored.has_value())
        QFAIL("no profile");
    QVERIFY(!stored->values.contains(QStringLiteral("password")));
}

void TstConnectionListModel::loadFailureIsReported()
{
    QTemporaryDir dir;
    // A directory where the database file should be: opening it fails.
    QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("q.db")));
    ConnectionListModel model(dir.filePath(QStringLiteral("q.db")));
    QAbstractItemModelTester tester(&model);
    model.start();
    QTRY_VERIFY_WITH_TIMEOUT(!model.lastError().isEmpty(), 10000);
    QVERIFY(!model.loading());
    QCOMPARE(model.rowCount(), 0);
}

void TstConnectionListModel::formModelFieldsAndValidation()
{
    ConnectionFormModel form;
    QAbstractItemModelTester tester(&form);
    ConnectionField file;
    file.key = QStringLiteral("file");
    file.label = QStringLiteral("File");
    file.kind = ConnectionField::Kind::Path;
    file.required = true;
    ConnectionField busy;
    busy.key = QStringLiteral("busy");
    busy.label = QStringLiteral("Busy");
    busy.kind = ConnectionField::Kind::Number;
    busy.defaultValue = 5000;
    busy.advanced = true;
    ConnectionField pw;
    pw.key = QStringLiteral("password");
    pw.kind = ConnectionField::Kind::Secret;
    form.setFields({ file, busy, pw });

    QCOMPARE(form.rowCount(), 2); // the secret field is not a row
    QVERIFY(form.hasSecret());
    QCOMPARE(form.secretKeys(), QStringList { QStringLiteral("password") });
    QVERIFY(form.hasAdvanced());
    QVERIFY(!form.isValid());
    QVERIFY(!form.data(form.index(0), ConnectionFormModel::ErrorRole).toString().isEmpty());

    form.setValue(0, QStringLiteral("a.db"));
    QVERIFY(form.isValid());
    form.setValue(1, QStringLiteral("abc"));
    QVERIFY(!form.isValid());
    QVERIFY(!form.data(form.index(1), ConnectionFormModel::ErrorRole).toString().isEmpty());
    form.setValue(1, QStringLiteral("250"));
    const QVariantMap v = form.values();
    QCOMPARE(v.value(QStringLiteral("file")).toString(), QStringLiteral("a.db"));
    QCOMPARE(v.value(QStringLiteral("busy")).toLongLong(), 250);
    QVERIFY(!v.contains(QStringLiteral("password")));
}

QTEST_MAIN(TstConnectionListModel)
#include "tst_connection_list_model.moc"
