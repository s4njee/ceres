#include <QtTest>

#include "engine/OutputParser.h"

// Canonical GNU rsync 3.x itemize output (NOT generated from the local
// openrsync, which can't produce progress2). Pinned fixtures are more robust
// than live capture anyway.
class OutputParserTest : public QObject {
    Q_OBJECT
private slots:
    void parsesItemizeLines();
    void handlesChunkBoundaries();
    void parsesProgress2();
    void parsesHumanReadableProgress2();
    void parsesProgress2WithoutToCheck();
    void parsesPerFileProgress();
    void routesStatsAndLog();
};

namespace {
const QByteArray kItemize =
    ">f+++++++++ file_new.txt\n"
    ">f.st...... file_changed.txt\n"
    "cd+++++++++ subdir/\n"
    "*deleting   old_file.txt\n";
}

void OutputParserTest::parsesItemizeLines()
{
    OutputParser p;
    QList<ChangeItem> items;
    connect(&p, &OutputParser::change, this,
            [&](const ChangeItem &i) { items.append(i); });

    p.feed(kItemize);

    QCOMPARE(items.size(), 4);

    QCOMPARE(items[0].path, QStringLiteral("file_new.txt"));
    QVERIFY(items[0].isNew);
    QVERIFY(!items[0].isDir);
    QCOMPARE(items[0].op, ChangeItem::Op::Update);

    QCOMPARE(items[1].path, QStringLiteral("file_changed.txt"));
    QVERIFY(!items[1].isNew);

    QCOMPARE(items[2].path, QStringLiteral("subdir/"));
    QVERIFY(items[2].isDir);
    QVERIFY(items[2].isNew);

    QCOMPARE(items[3].op, ChangeItem::Op::Deletion);
    QCOMPARE(items[3].path, QStringLiteral("old_file.txt"));
}

void OutputParserTest::handlesChunkBoundaries()
{
    OutputParser p;
    QList<ChangeItem> items;
    connect(&p, &OutputParser::change, this,
            [&](const ChangeItem &i) { items.append(i); });

    // Split mid-stream so a line is cut across two feed() calls.
    const int cut = 30;
    p.feed(kItemize.left(cut));
    p.feed(kItemize.mid(cut));

    QCOMPARE(items.size(), 4);
    QCOMPARE(items.last().path, QStringLiteral("old_file.txt"));
    QCOMPARE(items[1].path, QStringLiteral("file_changed.txt"));
}

void OutputParserTest::parsesProgress2()
{
    OutputParser p;
    QList<ProgressInfo> prog;
    connect(&p, &OutputParser::progress, this,
            [&](const ProgressInfo &i) { prog.append(i); });

    // An intermediate \r redraw followed by the final \n-terminated 100% update
    // — exercises both the \r and the \n code paths in one go.
    p.feed("        698,880  37%  666.34kB/s    0:00:01\r"
           "      1,232,896 100%  117.55MB/s    0:00:00 (xfr#1, to-chk=4/6)\n");

    QCOMPARE(prog.size(), 2);
    QCOMPARE(prog[0].percent, 37);
    QCOMPARE(prog[1].percent, 100);
    QCOMPARE(prog[1].bytes, qint64(1232896));
    QCOMPARE(prog[1].rate, QStringLiteral("117.55MB/s"));
    QCOMPARE(prog[1].xfr, 1);
    QCOMPARE(prog[1].toCheck, 4);
    QCOMPARE(prog[1].totalToCheck, 6);
}

void OutputParserTest::parsesHumanReadableProgress2()
{
    OutputParser p;
    QList<ProgressInfo> prog;
    connect(&p, &OutputParser::progress, this,
            [&](const ProgressInfo &i) { prog.append(i); });

    p.feed("          1.23M  42%   10.00MB/s    0:00:07 (xfr#2, to-chk=3/9)\r");
    p.feed(" ");

    QCOMPARE(prog.size(), 1);
    QCOMPARE(prog[0].percent, 42);
    QCOMPARE(prog[0].bytes, qint64(1289748));
    QCOMPARE(prog[0].rate, QStringLiteral("10.00MB/s"));
    QCOMPARE(prog[0].xfr, 2);
    QCOMPARE(prog[0].toCheck, 3);
    QCOMPARE(prog[0].totalToCheck, 9);
}

void OutputParserTest::parsesProgress2WithoutToCheck()
{
    OutputParser p;
    QList<ProgressInfo> prog;
    connect(&p, &OutputParser::progress, this,
            [&](const ProgressInfo &i) { prog.append(i); });

    p.feed("        698,880  37%  666.34kB/s    0:00:01\n");

    QCOMPARE(prog.size(), 1);
    QCOMPARE(prog[0].percent, 37);
    QCOMPARE(prog[0].bytes, qint64(698880));
    QCOMPARE(prog[0].toCheck, -1);
    QCOMPARE(prog[0].xfr, -1);
}

void OutputParserTest::parsesPerFileProgress()
{
    OutputParser p;
    p.setPerFileProgress(true);

    struct FileProg { QString path; int pct; };
    QList<FileProg> files;
    QList<int> agg;
    connect(&p, &OutputParser::fileProgress, this,
            [&](const QString &path, int pct, const QString &) { files.append({path, pct}); });
    connect(&p, &OutputParser::progress, this,
            [&](const ProgressInfo &i) { agg.append(i.percent); });

    // rsync --progress: each file's itemize line, then its own progress redraws,
    // ending with a \n completion line carrying to-chk=remaining/total.
    p.feed(">f+++++++++ dir/a.txt\n"
           "       512  50%    1.00MB/s    0:00:01\r"
           "     1,024 100%    1.00MB/s    0:00:00 (xfr#1, to-chk=1/2)\n"
           ">f+++++++++ dir/b.txt\n"
           "     2,048 100%    2.00MB/s    0:00:00 (xfr#2, to-chk=0/2)\n");

    // Per-file: a@50, a@100, b@100 — each tagged with the right path.
    QCOMPARE(files.size(), 3);
    QCOMPARE(files[0].path, QStringLiteral("dir/a.txt"));
    QCOMPARE(files[0].pct, 50);
    QCOMPARE(files[1].path, QStringLiteral("dir/a.txt"));
    QCOMPARE(files[1].pct, 100);
    QCOMPARE(files[2].path, QStringLiteral("dir/b.txt"));
    QCOMPARE(files[2].pct, 100);

    // Aggregate is derived from completed-file count: 1/2 -> 50%, then 2/2 -> 100%.
    QVERIFY(agg.contains(50));
    QCOMPARE(agg.last(), 100);
}

void OutputParserTest::routesStatsAndLog()
{
    OutputParser p;
    QStringList stats;
    QStringList logs;
    connect(&p, &OutputParser::stats, this, [&](const QString &l) { stats.append(l); });
    connect(&p, &OutputParser::log, this, [&](const QString &l) { logs.append(l); });

    p.feed("Number of files: 6 (reg: 4, dir: 2)\n");
    p.feed("some unrecognised note\n");

    QCOMPARE(stats.size(), 1);
    QVERIFY(stats[0].startsWith(QStringLiteral("Number of files")));
    QVERIFY(logs.contains(QStringLiteral("some unrecognised note")));
}

QTEST_MAIN(OutputParserTest)
#include "tst_outputparser.moc"
