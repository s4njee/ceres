#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "app/PathCompleter.h"

class PathCompleterTest : public QObject {
    Q_OBJECT
private slots:
    void completesLongestCommonPrefix();
    void completesSingleDirWithSlash();
    void singleFileHasNoSlash();
    void noMatchReturnsInput();
};

static void touch(const QString &path)
{
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write("x");
    f.close();
}

void PathCompleterTest::completesLongestCommonPrefix()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("alpha")));  // dir
    touch(tmp.path() + QStringLiteral("/alps"));               // file
    touch(tmp.path() + QStringLiteral("/beta"));

    PathCompleter c;
    // "al" matches alpha + alps -> common prefix "alp" (two matches, no slash)
    QCOMPARE(c.completeLocal(tmp.path() + QStringLiteral("/al")), tmp.path() + QStringLiteral("/alp"));
}

void PathCompleterTest::completesSingleDirWithSlash()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("alpha")));
    touch(tmp.path() + QStringLiteral("/beta"));

    PathCompleter c;
    QCOMPARE(c.completeLocal(tmp.path() + QStringLiteral("/alp")),
             tmp.path() + QStringLiteral("/alpha/"));  // lone dir -> trailing slash
}

void PathCompleterTest::singleFileHasNoSlash()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    touch(tmp.path() + QStringLiteral("/beta"));

    PathCompleter c;
    QCOMPARE(c.completeLocal(tmp.path() + QStringLiteral("/bet")),
             tmp.path() + QStringLiteral("/beta"));  // lone file -> no slash
}

void PathCompleterTest::noMatchReturnsInput()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    PathCompleter c;
    const QString none = tmp.path() + QStringLiteral("/zzz");
    QCOMPARE(c.completeLocal(none), none);
}

QTEST_MAIN(PathCompleterTest)
#include "tst_pathcompleter.moc"
