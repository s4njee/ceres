#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "engine/BinaryLocator.h"

class BinaryLocatorTest : public QObject {
    Q_OBJECT
private slots:
    void probeAcceptsValidVersion();
    void probeRejectsFailedOrMalformedExecutables();
};

static QString writeScript(const QString &dir, const QString &name, const QByteArray &body)
{
    const QString path = dir + QLatin1Char('/') + name;
    QFile f(path);
    const bool opened = f.open(QIODevice::WriteOnly);
    Q_ASSERT(opened);
    if (!opened)
        return path;
    f.write(body);
    f.close();
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return path;
}

void BinaryLocatorTest::probeAcceptsValidVersion()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString script = writeScript(tmp.path(), QStringLiteral("rsync-ok"),
                                       "#!/bin/sh\nprintf 'rsync  version 3.2.7  protocol version 31\\n'\n");

    const RsyncCapabilities caps = BinaryLocator::probe(script);
    QVERIFY(caps.found);
    QCOMPARE(caps.major, 3);
    QCOMPARE(caps.minor, 2);
    QCOMPARE(caps.versionString, QStringLiteral("3.2"));
}

void BinaryLocatorTest::probeRejectsFailedOrMalformedExecutables()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString failed = writeScript(tmp.path(), QStringLiteral("rsync-fail"),
                                       "#!/bin/sh\nprintf 'rsync version 3.2.7\\n'\nexit 2\n");
    const QString malformed = writeScript(tmp.path(), QStringLiteral("rsync-weird"),
                                          "#!/bin/sh\nprintf 'definitely not rsync\\n'\n");

    QVERIFY(!BinaryLocator::probe(failed).found);
    QVERIFY(!BinaryLocator::probe(malformed).found);
}

QTEST_MAIN(BinaryLocatorTest)
#include "tst_binarylocator.moc"
