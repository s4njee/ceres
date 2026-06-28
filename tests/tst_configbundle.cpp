#include <QtTest>

#include "core/ConfigBundle.h"

class ConfigBundleTest : public QObject {
    Q_OBJECT
private slots:
    void roundTrips();
    void rejectsGarbage();
    void toleratesMissingSections();
};

static ConfigBundle::Data sample()
{
    ConfigBundle::Data d;

    SshHost h;
    h.target = QStringLiteral("deploy@10.0.0.5");
    h.host = QStringLiteral("10.0.0.5");
    h.user = QStringLiteral("deploy");
    h.label = QStringLiteral("Prod");
    h.sshKeyPath = QStringLiteral("~/.ssh/id_ed25519");
    h.sshPort = 2222;
    h.hasPassword = true;
    d.hosts << h;

    PairedDevice dev;
    dev.deviceId = QStringLiteral("dev-1");
    dev.name = QStringLiteral("Studio NAS");
    dev.sshTarget = QStringLiteral("backup@nas.local");
    dev.pairedAtMs = 1719500000000LL;
    d.devices << dev;

    d.bookmarks.insert(QStringLiteral("deploy@10.0.0.5"),
                       {QStringLiteral("/srv/www/"), QStringLiteral("/var/log/")});
    d.editorCommand = QStringLiteral("code -w");
    return d;
}

void ConfigBundleTest::roundTrips()
{
    const ConfigBundle::Data in = sample();
    ConfigBundle::Data out;
    QVERIFY(ConfigBundle::fromJson(ConfigBundle::toJson(in), out));

    QCOMPARE(out.hosts.size(), 1);
    QCOMPARE(out.hosts.first().target, QStringLiteral("deploy@10.0.0.5"));
    QCOMPARE(out.hosts.first().sshPort, 2222);
    QVERIFY(out.hosts.first().hasPassword);

    QCOMPARE(out.devices.size(), 1);
    QCOMPARE(out.devices.first().deviceId, QStringLiteral("dev-1"));
    QCOMPARE(out.devices.first().pairedAtMs, 1719500000000LL);

    QCOMPARE(out.bookmarks.value(QStringLiteral("deploy@10.0.0.5")).size(), 2);
    QCOMPARE(out.editorCommand, QStringLiteral("code -w"));

    // No secret material leaks into the bundle.
    QVERIFY(!ConfigBundle::toJson(in).contains("password\""));
}

void ConfigBundleTest::rejectsGarbage()
{
    ConfigBundle::Data out;
    QVERIFY(!ConfigBundle::fromJson(QByteArrayLiteral("not json"), out));
    QVERIFY(!ConfigBundle::fromJson(QByteArrayLiteral("{\"app\":\"SomethingElse\"}"), out));
}

void ConfigBundleTest::toleratesMissingSections()
{
    // A bundle with only hosts still parses; other sections come back empty.
    ConfigBundle::Data out;
    QVERIFY(ConfigBundle::fromJson(
        QByteArrayLiteral("{\"app\":\"Ceres\",\"hosts\":[{\"target\":\"u@h\"}]}"), out));
    QCOMPARE(out.hosts.size(), 1);
    QVERIFY(out.devices.isEmpty());
    QVERIFY(out.bookmarks.isEmpty());
}

QTEST_MAIN(ConfigBundleTest)
#include "tst_configbundle.moc"
