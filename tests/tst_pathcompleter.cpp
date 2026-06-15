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
    void bareTildeExpandsToHome();
    void localChoicesUseFullPaths();
    void localChoicesSuppressTooManyMatches();
};

static bool touch(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write("x") == 1;
}

void PathCompleterTest::completesLongestCommonPrefix()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("alpha")));  // dir
    QVERIFY(touch(tmp.path() + QStringLiteral("/alps")));      // file
    QVERIFY(touch(tmp.path() + QStringLiteral("/beta")));

    PathCompleter c;
    // "al" matches alpha + alps -> common prefix "alp" (two matches, no slash)
    QCOMPARE(c.completeLocal(tmp.path() + QStringLiteral("/al")), tmp.path() + QStringLiteral("/alp"));
}

void PathCompleterTest::completesSingleDirWithSlash()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("alpha")));
    QVERIFY(touch(tmp.path() + QStringLiteral("/beta")));

    PathCompleter c;
    QCOMPARE(c.completeLocal(tmp.path() + QStringLiteral("/alp")),
             tmp.path() + QStringLiteral("/alpha/"));  // lone dir -> trailing slash
}

void PathCompleterTest::singleFileHasNoSlash()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QVERIFY(touch(tmp.path() + QStringLiteral("/beta")));

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

void PathCompleterTest::bareTildeExpandsToHome()
{
    PathCompleter c;
    QCOMPARE(c.completeLocal(QStringLiteral("~")), QDir::homePath() + QStringLiteral("/"));
}

void PathCompleterTest::localChoicesUseFullPaths()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("alpha")));
    QVERIFY(touch(tmp.path() + QStringLiteral("/alps")));
    QVERIFY(touch(tmp.path() + QStringLiteral("/beta")));

    PathCompleter c;
    const QStringList choices = c.localChoices(tmp.path() + QStringLiteral("/al"), 8);
    QCOMPARE(choices.size(), 2);
    QCOMPARE(choices.at(0), tmp.path() + QStringLiteral("/alpha/"));
    QCOMPARE(choices.at(1), tmp.path() + QStringLiteral("/alps"));
}

void PathCompleterTest::localChoicesSuppressTooManyMatches()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("one")));
    QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("two")));
    QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("three")));

    PathCompleter c;
    QVERIFY(c.localChoices(tmp.path() + QStringLiteral("/"), 2).isEmpty());
}

QTEST_MAIN(PathCompleterTest)
#include "tst_pathcompleter.moc"
