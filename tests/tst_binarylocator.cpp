#include <QtTest>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include "engine/BinaryLocator.h"

class BinaryLocatorTest : public QObject {
    Q_OBJECT
private slots:
    void probeAcceptsValidVersion();
    void probeRejectsFailedOrMalformedExecutables();
    void bundledCandidatesAreAppRelative();
    void detectsRuntimePathStyle();
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
#ifdef Q_OS_WIN
    // A `#!/bin/sh` script can't be executed on Windows (no shebang support, and
    // a file without an executable extension isn't runnable), so probe() can't
    // exercise a fake binary here. The real bundled rsync.exe is covered by the
    // Windows runtime smoke test instead.
    QSKIP("shebang fake-binary is not executable on Windows");
#endif
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

void BinaryLocatorTest::bundledCandidatesAreAppRelative()
{
    const QString dir = QCoreApplication::applicationDirPath();
    const QStringList cands = BinaryLocator::bundledRsyncCandidates();
    QVERIFY(!cands.isEmpty());

#ifdef Q_OS_WIN
    const QString exe = QStringLiteral("rsync.exe");
#else
    const QString exe = QStringLiteral("rsync");
#endif
    // Highest-priority candidate is rsync sitting directly beside the app.
    QCOMPARE(cands.first(), dir + QLatin1Char('/') + exe);
    // Every candidate lives under the app directory and ends in the platform exe.
    for (const QString &c : cands) {
        QVERIFY(c.startsWith(dir));
        QVERIFY(c.endsWith(exe));
    }
}

void BinaryLocatorTest::detectsRuntimePathStyle()
{
    using PS = RsyncCapabilities::PathStyle;
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString rsync = writeScript(tmp.path(), QStringLiteral("rsync"),
                                      "#!/bin/sh\nprintf 'rsync  version 3.2.7\\n'\n");

#ifndef Q_OS_WIN
    // A plain directory with no Cygwin/MSYS runtime DLL: Native off Windows.
    QVERIFY(BinaryLocator::detectPathStyle(rsync) == PS::Native);
#endif

    // cygwin1.dll beside the binary -> Cygwin path style.
    { QFile f(tmp.path() + QStringLiteral("/cygwin1.dll")); QVERIFY(f.open(QIODevice::WriteOnly)); }
    QVERIFY(BinaryLocator::detectPathStyle(rsync) == PS::Cygwin);

    // msys-2.0.dll takes precedence -> MSYS2 path style.
    { QFile f(tmp.path() + QStringLiteral("/msys-2.0.dll")); QVERIFY(f.open(QIODevice::WriteOnly)); }
    QVERIFY(BinaryLocator::detectPathStyle(rsync) == PS::Msys);
}

QTEST_MAIN(BinaryLocatorTest)
#include "tst_binarylocator.moc"
