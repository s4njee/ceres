#include <QtTest>

#include "models/FileListModel.h"

class FileListModelTest : public QObject {
    Q_OBJECT
private slots:
    void humanSizeFormats();
    void sortsDirsFirstThenName();
    void sortBySizeAndDirection();
    void sortByDateUsesEpoch();
    void filterMatchesNameSubstring();
    void rolesExposeEntryFields();
    void clearEmpties();
};

void FileListModelTest::humanSizeFormats()
{
    QCOMPARE(FileListModel::humanSize(0), QStringLiteral("0 B"));
    QCOMPARE(FileListModel::humanSize(512), QStringLiteral("512 B"));
    QCOMPARE(FileListModel::humanSize(1536), QStringLiteral("1.5 KB"));   // one decimal under 10
    QCOMPARE(FileListModel::humanSize(24 * 1024), QStringLiteral("24 KB")); // none at/above 10
    QCOMPARE(FileListModel::humanSize(1024LL * 1024), QStringLiteral("1.0 MB"));
    QCOMPARE(FileListModel::humanSize(1024LL * 1024 * 1024 * 2), QStringLiteral("2.0 GB"));
}

static FileEntry makeEntry(const QString &name, bool isDir, qint64 size = 0)
{
    FileEntry e;
    e.name = name;
    e.isDir = isDir;
    e.size = size;
    e.mtimeText = QStringLiteral("Jun 22  10:30");
    return e;
}

void FileListModelTest::sortsDirsFirstThenName()
{
    FileListModel m;
    m.setEntries({makeEntry("zeta.txt", false), makeEntry("alpha", true),
                  makeEntry("beta.txt", false), makeEntry("Gamma", true)});

    QCOMPARE(m.rowCount(), 4);
    const auto nameAt = [&](int row) {
        return m.data(m.index(row), FileListModel::NameRole).toString();
    };
    // Directories first (case-insensitive name order), then files.
    QCOMPARE(nameAt(0), QStringLiteral("alpha"));
    QCOMPARE(nameAt(1), QStringLiteral("Gamma"));
    QCOMPARE(nameAt(2), QStringLiteral("beta.txt"));
    QCOMPARE(nameAt(3), QStringLiteral("zeta.txt"));
    QVERIFY(m.isDirAt(0));
    QVERIFY(!m.isDirAt(2));
}

void FileListModelTest::sortBySizeAndDirection()
{
    FileListModel m;
    m.setEntries({makeEntry("big.bin", false, 9000), makeEntry("small.txt", false, 10),
                  makeEntry("mid.dat", false, 500), makeEntry("dir", true)});
    const auto nameAt = [&](int row) {
        return m.data(m.index(row), FileListModel::NameRole).toString();
    };

    m.setSort(FileListModel::BySize, /*ascending=*/true);
    QCOMPARE(nameAt(0), QStringLiteral("dir"));        // dirs stay grouped first
    QCOMPARE(nameAt(1), QStringLiteral("small.txt"));  // then files ascending by size
    QCOMPARE(nameAt(2), QStringLiteral("mid.dat"));
    QCOMPARE(nameAt(3), QStringLiteral("big.bin"));

    m.setSort(FileListModel::BySize, /*ascending=*/false);
    QCOMPARE(nameAt(0), QStringLiteral("dir"));        // dirs still first, independent of direction
    QCOMPARE(nameAt(1), QStringLiteral("big.bin"));
    QCOMPARE(nameAt(3), QStringLiteral("small.txt"));
}

void FileListModelTest::sortByDateUsesEpoch()
{
    FileEntry older = makeEntry("older.txt", false);
    older.mtime = 1000;
    FileEntry newer = makeEntry("newer.txt", false);
    newer.mtime = 2000;

    FileListModel m;
    m.setEntries({older, newer});
    const auto nameAt = [&](int row) {
        return m.data(m.index(row), FileListModel::NameRole).toString();
    };

    m.setSort(FileListModel::ByDate, /*ascending=*/true);
    QCOMPARE(nameAt(0), QStringLiteral("older.txt"));
    QCOMPARE(nameAt(1), QStringLiteral("newer.txt"));

    m.setSort(FileListModel::ByDate, /*ascending=*/false);
    QCOMPARE(nameAt(0), QStringLiteral("newer.txt"));
}

void FileListModelTest::filterMatchesNameSubstring()
{
    FileListModel m;
    m.setEntries({makeEntry("report.pdf", false), makeEntry("notes.txt", false),
                  makeEntry("reports", true), makeEntry("image.png", false)});
    QCOMPARE(m.rowCount(), 4);

    m.setFilter(QStringLiteral("REP"));  // case-insensitive substring, applies to dirs too
    QCOMPARE(m.rowCount(), 2);
    const auto nameAt = [&](int row) {
        return m.data(m.index(row), FileListModel::NameRole).toString();
    };
    QCOMPARE(nameAt(0), QStringLiteral("reports"));   // dir still groups first
    QCOMPARE(nameAt(1), QStringLiteral("report.pdf"));

    m.setFilter(QString());  // clearing restores the full listing
    QCOMPARE(m.rowCount(), 4);
}

void FileListModelTest::rolesExposeEntryFields()
{
    FileListModel m;
    m.setEntries({makeEntry("report.pdf", false, 1536), makeEntry("docs", true)});

    // report.pdf sorts after the dir, so it's row 1.
    const QModelIndex file = m.index(1);
    QCOMPARE(m.data(file, FileListModel::NameRole).toString(), QStringLiteral("report.pdf"));
    QCOMPARE(m.data(file, FileListModel::IsDirRole).toBool(), false);
    QCOMPARE(m.data(file, FileListModel::SizeRole).toLongLong(), 1536LL);
    QCOMPARE(m.data(file, FileListModel::SizeTextRole).toString(), QStringLiteral("1.5 KB"));

    // Directories report no size text.
    const QModelIndex dir = m.index(0);
    QCOMPARE(m.data(dir, FileListModel::IsDirRole).toBool(), true);
    QVERIFY(m.data(dir, FileListModel::SizeTextRole).toString().isEmpty());
}

void FileListModelTest::clearEmpties()
{
    FileListModel m;
    m.setEntries({makeEntry("a", false), makeEntry("b", true)});
    QCOMPARE(m.rowCount(), 2);
    m.clear();
    QCOMPARE(m.rowCount(), 0);
}

QTEST_MAIN(FileListModelTest)
#include "tst_filelistmodel.moc"
