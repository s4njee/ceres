#include <QtTest>

#include "app/RemoteFs.h"

class RemoteFsTest : public QObject {
    Q_OBJECT
private slots:
    void parsesGnuListing();
    void parsesBsdListing();
    void emptyInputs();
};

void RemoteFsTest::parsesGnuListing()
{
    // GNU/Linux `ls -lA`: a total header, a directory, two files (one with spaces in
    // the name), and a symlink. Names with spaces must survive; the symlink target
    // must be stripped from the name.
    const QString block = QStringLiteral(
        "total 24\n"
        "drwxr-xr-x  2 alice users  4096 Jun 22 10:30 projects\n"
        "-rw-r--r--  1 alice users 12345 Jun 22 10:30 notes.txt\n"
        "-rw-r--r--  1 alice users 99999 Jun 20 09:15 my report.pdf\n"
        "lrwxrwxrwx  1 alice users    11 Jun 18 08:00 current -> /opt/app/v2\n");

    const QList<FileEntry> entries = RemoteFs::parseLsList(block);
    QCOMPARE(entries.size(), 4);

    QCOMPARE(entries.at(0).name, QStringLiteral("projects"));
    QVERIFY(entries.at(0).isDir);
    QVERIFY(!entries.at(0).isSymlink);
    QCOMPARE(entries.at(0).size, qint64(4096));
    QCOMPARE(entries.at(0).mtimeText, QStringLiteral("Jun 22 10:30"));

    QCOMPARE(entries.at(1).name, QStringLiteral("notes.txt"));
    QVERIFY(!entries.at(1).isDir);
    QVERIFY(!entries.at(1).isSymlink);
    QCOMPARE(entries.at(1).size, qint64(12345));

    // Spaces in the name are preserved verbatim.
    QCOMPARE(entries.at(2).name, QStringLiteral("my report.pdf"));
    QVERIFY(!entries.at(2).isDir);
    QCOMPARE(entries.at(2).size, qint64(99999));

    // Symlink: flagged as such, target stripped, treated as a file (not a dir).
    QCOMPARE(entries.at(3).name, QStringLiteral("current"));
    QVERIFY(entries.at(3).isSymlink);
    QVERIFY(!entries.at(3).isDir);
}

void RemoteFsTest::parsesBsdListing()
{
    // BSD/macOS `ls -lA`: different column widths (BSD pads the link count and shows a
    // distinct owner/group). The same logical entries must parse out.
    const QString block = QStringLiteral(
        "total 48\n"
        "drwxr-xr-x   4 alice  staff   128 Jun 22 10:30 projects\n"
        "-rw-r--r--   1 alice  staff 12345 Jun 22 10:30 notes.txt\n"
        "-rw-r--r--   1 alice  staff 99999 Jun 20 09:15 my report.pdf\n"
        "lrwxr-xr-x   1 alice  staff    11 Jun 18 08:00 current -> /opt/app/v2\n");

    const QList<FileEntry> entries = RemoteFs::parseLsList(block);
    QCOMPARE(entries.size(), 4);

    QCOMPARE(entries.at(0).name, QStringLiteral("projects"));
    QVERIFY(entries.at(0).isDir);
    QCOMPARE(entries.at(0).size, qint64(128));

    QCOMPARE(entries.at(1).name, QStringLiteral("notes.txt"));
    QCOMPARE(entries.at(1).size, qint64(12345));

    QCOMPARE(entries.at(2).name, QStringLiteral("my report.pdf"));
    QCOMPARE(entries.at(2).size, qint64(99999));

    QCOMPARE(entries.at(3).name, QStringLiteral("current"));
    QVERIFY(entries.at(3).isSymlink);
    QVERIFY(!entries.at(3).isDir);
}

void RemoteFsTest::emptyInputs()
{
    QVERIFY(RemoteFs::parseLsList(QString()).isEmpty());
    QVERIFY(RemoteFs::parseLsList(QStringLiteral("")).isEmpty());
    // A directory with no entries still emits the byte-total header and nothing else.
    QVERIFY(RemoteFs::parseLsList(QStringLiteral("total 0\n")).isEmpty());
}

QTEST_MAIN(RemoteFsTest)
#include "tst_remotefs.moc"
