#include <QtTest>

#include <QList>
#include <QVariantList>
#include <QVariantMap>

#include "app/TransferManager.h"
#include "engine/SyncEngine.h"
#include "models/TransfersModel.h"

// Test double mirroring tst_jobcontroller.cpp's FakeEngine: records start()
// calls and lets the test drive lifecycle via finish()/sendProgress().
class FakeEngine : public SyncEngine {
    Q_OBJECT
public:
    int starts = 0;
    int cancels = 0;
    int pauses = 0;
    int resumes = 0;
    bool running = false;
    SyncJob lastJob;

    void start(const SyncJob &job, bool dryRun) override
    {
        ++starts;
        lastJob = job;
        Q_UNUSED(dryRun);
        running = true;
        emit started();
    }

    void cancel() override
    {
        ++cancels;
        running = false;
    }

    void pause() override { ++pauses; }
    void resume() override { ++resumes; }

    bool isRunning() const override { return running; }

    void finish(int code = 0, bool crashed = false)
    {
        running = false;
        emit finished(code, crashed);
    }

    void sendProgress(int percent, const QString &rate)
    {
        ProgressInfo info;
        info.percent = percent;
        info.rate = rate;
        emit progress(info);
    }

    void sendChange(const QString &path)
    {
        ChangeItem item;
        item.op = ChangeItem::Op::Update;
        item.fileType = QLatin1Char('f');
        item.path = path;
        emit change(item);
    }

    void sendFileProgress(const QString &path, int percent, const QString &rate)
    {
        emit fileProgress(path, percent, rate);
    }

    void failStart(const QString &reason) { emit failedToStart(reason); }
};

// Hands out FakeEngine instances and keeps references so the test can drive
// them. The manager takes ownership (setParent), so we don't delete them here.
class FakeEngineFactory {
public:
    QList<FakeEngine *> created;

    SyncEngine *operator()()
    {
        auto *e = new FakeEngine;
        created.append(e);
        return e;
    }
};

class TransferManagerTest : public QObject {
    Q_OBJECT
private slots:
    void capRespected();
    void queueDrains();
    void failureMarksFailed();
    void cancelQueuedAndActive();
    void enqueuedSignalFiresPerEnqueue();
    void pauseAndResume();
    void fileProgressBuildsTree();
    void seedShowsFilesUpfrontAndReconciles();
    void seedDoesNotResetLivePaths();
    void retryResubmitsFailedTransfer();
    void allCompleteFiresWhenQueueDrains();
};

// Find a file row (from the FilesRole tree) by its path, or an empty map if absent.
static QVariantMap fileByPath(TransfersModel *m, int row, const QString &path)
{
    const QVariantList files = m->data(m->index(row), TransfersModel::FilesRole).toList();
    for (const QVariant &v : files) {
        const QVariantMap map = v.toMap();
        if (map.value(QStringLiteral("path")).toString() == path)
            return map;
    }
    return {};
}

static SyncJob jobN(int n)
{
    SyncJob j;
    j.source = QStringLiteral("/tmp/src%1/").arg(n);
    j.destination = QStringLiteral("/tmp/dst%1/").arg(n);
    return j;
}

// Count rows in a given status by reading the model the way QML would.
static int countStatus(TransfersModel *m, TransfersModel::Status status)
{
    int n = 0;
    for (int i = 0; i < m->rowCount(); ++i) {
        const QModelIndex mi = m->index(i);
        if (m->data(mi, TransfersModel::StatusRole).toInt() == static_cast<int>(status))
            ++n;
    }
    return n;
}

void TransferManagerTest::capRespected()
{
    FakeEngineFactory factory;
    TransferManager mgr(std::ref(factory));
    mgr.setMaxConcurrent(2);

    for (int i = 0; i < 4; ++i)
        mgr.enqueue(jobN(i), QStringLiteral("up"), QStringLiteral("t%1").arg(i));

    // Exactly two engines started; two rows Active, two still Queued.
    QCOMPARE(factory.created.size(), 2);
    QCOMPARE(mgr.activeCount(), 2);
    QCOMPARE(countStatus(mgr.model(), TransfersModel::Active), 2);
    QCOMPARE(countStatus(mgr.model(), TransfersModel::Queued), 2);

    // Finishing one admits a third from the queue.
    factory.created.at(0)->finish(0);
    QCOMPARE(factory.created.size(), 3);
    QCOMPARE(mgr.activeCount(), 2);
    QCOMPARE(countStatus(mgr.model(), TransfersModel::Done), 1);
    QCOMPARE(countStatus(mgr.model(), TransfersModel::Active), 2);
    QCOMPARE(countStatus(mgr.model(), TransfersModel::Queued), 1);
}

void TransferManagerTest::queueDrains()
{
    FakeEngineFactory factory;
    TransferManager mgr(std::ref(factory));
    mgr.setMaxConcurrent(2);

    for (int i = 0; i < 4; ++i)
        mgr.enqueue(jobN(i), QStringLiteral("down"), QStringLiteral("t%1").arg(i));

    // Finish each engine as it appears; the queue should fully drain to 4 engines.
    int finished = 0;
    while (finished < 4) {
        QVERIFY(finished < factory.created.size());
        factory.created.at(finished)->finish(0);
        ++finished;
    }

    QCOMPARE(factory.created.size(), 4);
    QCOMPARE(mgr.activeCount(), 0);
    QCOMPARE(countStatus(mgr.model(), TransfersModel::Done), 4);
    QCOMPARE(mgr.model()->activeCount(), 0);
}

void TransferManagerTest::failureMarksFailed()
{
    FakeEngineFactory factory;
    TransferManager mgr(std::ref(factory));
    mgr.setMaxConcurrent(2);

    const QString id0 = mgr.enqueue(jobN(0), QStringLiteral("up"), QStringLiteral("t0"));
    mgr.enqueue(jobN(1), QStringLiteral("up"), QStringLiteral("t1"));

    factory.created.at(0)->finish(23);  // non-zero exit
    QCOMPARE(countStatus(mgr.model(), TransfersModel::Failed), 1);

    // Verify the failed row is the first transfer's row.
    const QModelIndex mi = mgr.model()->index(0);
    QCOMPARE(mgr.model()->data(mi, TransfersModel::IdRole).toString(), id0);
    QCOMPARE(mgr.model()->data(mi, TransfersModel::StatusRole).toInt(),
             static_cast<int>(TransfersModel::Failed));
}

void TransferManagerTest::cancelQueuedAndActive()
{
    FakeEngineFactory factory;
    TransferManager mgr(std::ref(factory));
    mgr.setMaxConcurrent(1);

    const QString idActive = mgr.enqueue(jobN(0), QStringLiteral("up"), QStringLiteral("t0"));
    const QString idQueued = mgr.enqueue(jobN(1), QStringLiteral("up"), QStringLiteral("t1"));

    QCOMPARE(factory.created.size(), 1);  // only the first started under cap=1

    // Cancelling the still-queued transfer marks it Cancelled and never starts it.
    mgr.cancel(idQueued);
    QCOMPARE(factory.created.size(), 1);  // no new engine
    QCOMPARE(countStatus(mgr.model(), TransfersModel::Cancelled), 1);

    // Cancelling the active transfer calls cancel() on its engine; the engine
    // then reports an interrupted run (crashed=true), which becomes Cancelled.
    mgr.cancel(idActive);
    QCOMPARE(factory.created.at(0)->cancels, 1);
    factory.created.at(0)->finish(0, /*crashed=*/true);

    QCOMPARE(mgr.activeCount(), 0);
    QCOMPARE(countStatus(mgr.model(), TransfersModel::Cancelled), 2);
    Q_UNUSED(idActive);
}

void TransferManagerTest::enqueuedSignalFiresPerEnqueue()
{
    FakeEngineFactory factory;
    TransferManager mgr(std::ref(factory));
    mgr.setMaxConcurrent(2);

    QSignalSpy spy(&mgr, &TransferManager::enqueued);

    mgr.enqueue(jobN(0), QStringLiteral("up"), QStringLiteral("t0"));
    QCOMPARE(spy.count(), 1);

    mgr.enqueue(jobN(1), QStringLiteral("up"), QStringLiteral("t1"));
    mgr.enqueue(jobN(2), QStringLiteral("up"), QStringLiteral("t2"));
    QCOMPARE(spy.count(), 3);
}

void TransferManagerTest::pauseAndResume()
{
    FakeEngineFactory factory;
    TransferManager mgr(std::ref(factory));
    mgr.setMaxConcurrent(1);
    TransfersModel *model = mgr.model();

    const QString a = mgr.enqueue(jobN(1), QStringLiteral("down"), QStringLiteral("a"));
    const QString b = mgr.enqueue(jobN(2), QStringLiteral("down"), QStringLiteral("b"));
    // a is Active (cap 1), b is Queued.
    QCOMPARE(countStatus(model, TransfersModel::Active), 1);
    QCOMPARE(countStatus(model, TransfersModel::Queued), 1);

    // Pausing the active transfer suspends its engine and holds its slot: b stays
    // queued (a paused-active transfer does not free the slot).
    mgr.pause(a);
    QCOMPARE(factory.created.at(0)->pauses, 1);
    QCOMPARE(countStatus(model, TransfersModel::Paused), 1);
    QCOMPARE(countStatus(model, TransfersModel::Queued), 1);
    QCOMPARE(factory.created.size(), 1);  // b was NOT started

    // Resuming continues the engine and frees nothing new (still 1 active).
    mgr.resume(a);
    QCOMPARE(factory.created.at(0)->resumes, 1);
    QCOMPARE(countStatus(model, TransfersModel::Active), 1);

    // Pausing a *queued* transfer holds it out of pump(): finishing a lets the queue
    // advance, but the paused b must not start.
    mgr.pause(b);
    QCOMPARE(countStatus(model, TransfersModel::Paused), 1);
    factory.created.at(0)->finish(0);  // a completes
    QCOMPARE(countStatus(model, TransfersModel::Done), 1);
    QCOMPARE(factory.created.size(), 1);  // b still not started (paused)

    // Resuming b lets pump() start it.
    mgr.resume(b);
    QCOMPARE(factory.created.size(), 2);
    QCOMPARE(countStatus(model, TransfersModel::Active), 1);
}

void TransferManagerTest::fileProgressBuildsTree()
{
    FakeEngineFactory factory;
    TransferManager mgr(std::ref(factory));
    mgr.setMaxConcurrent(1);

    mgr.enqueue(jobN(1), QStringLiteral("down"), QStringLiteral("folder"));
    QCOMPARE(factory.created.size(), 1);

    factory.created.at(0)->sendChange(QStringLiteral("dir/sub/a.txt"));
    factory.created.at(0)->sendFileProgress(QStringLiteral("dir/sub/a.txt"), 40,
                                            QStringLiteral("10.00kB/s"));
    factory.created.at(0)->sendChange(QStringLiteral("dir/b.txt"));
    factory.created.at(0)->sendFileProgress(QStringLiteral("dir/b.txt"), 100,
                                            QStringLiteral("20.00kB/s"));

    const QModelIndex row = mgr.model()->index(0);
    QCOMPARE(mgr.model()->data(row, TransfersModel::FileCountRole).toInt(), 2);

    const QVariantList files = mgr.model()->data(row, TransfersModel::FilesRole).toList();
    QCOMPARE(files.size(), 4);

    const auto asMap = [](const QVariant &v) { return v.toMap(); };
    QCOMPARE(asMap(files.at(0)).value(QStringLiteral("name")).toString(), QStringLiteral("dir"));
    QCOMPARE(asMap(files.at(0)).value(QStringLiteral("isDir")).toBool(), true);
    QCOMPARE(asMap(files.at(0)).value(QStringLiteral("depth")).toInt(), 0);

    QCOMPARE(asMap(files.at(1)).value(QStringLiteral("name")).toString(), QStringLiteral("sub"));
    QCOMPARE(asMap(files.at(1)).value(QStringLiteral("isDir")).toBool(), true);
    QCOMPARE(asMap(files.at(1)).value(QStringLiteral("depth")).toInt(), 1);

    QCOMPARE(asMap(files.at(2)).value(QStringLiteral("name")).toString(), QStringLiteral("a.txt"));
    QCOMPARE(asMap(files.at(2)).value(QStringLiteral("isDir")).toBool(), false);
    QCOMPARE(asMap(files.at(2)).value(QStringLiteral("depth")).toInt(), 2);
    QCOMPARE(asMap(files.at(2)).value(QStringLiteral("percent")).toInt(), 40);

    QCOMPARE(asMap(files.at(3)).value(QStringLiteral("name")).toString(), QStringLiteral("b.txt"));
    QCOMPARE(asMap(files.at(3)).value(QStringLiteral("isDir")).toBool(), false);
    QCOMPARE(asMap(files.at(3)).value(QStringLiteral("depth")).toInt(), 1);
    QCOMPARE(asMap(files.at(3)).value(QStringLiteral("percent")).toInt(), 100);
}

void TransferManagerTest::seedShowsFilesUpfrontAndReconciles()
{
    FakeEngineFactory factory;
    TransferManager mgr(std::ref(factory));
    mgr.setMaxConcurrent(1);
    TransfersModel *model = mgr.model();

    const QString id = mgr.enqueue(jobN(1), QStringLiteral("up"), QStringLiteral("folder"));

    // Seed the full list a source-side walk would produce: the whole tree at 0%,
    // before any byte moves.
    mgr.seedFiles(id, {QStringLiteral("folder/a.txt"), QStringLiteral("folder/sub/b.txt"),
                       QStringLiteral("folder/sub/c.txt")});

    // All three leaves are present immediately, each at 0%, plus synthesized folders.
    QCOMPARE(model->data(model->index(0), TransfersModel::FileCountRole).toInt(), 3);
    QCOMPARE(fileByPath(model, 0, QStringLiteral("folder/a.txt"))
                 .value(QStringLiteral("percent")).toInt(), 0);
    QVERIFY(!fileByPath(model, 0, QStringLiteral("folder/sub/")).isEmpty());  // folder row exists

    // The real run transfers two of the three; c.txt is already current, so rsync
    // never reports it.
    factory.created.at(0)->sendFileProgress(QStringLiteral("folder/a.txt"), 100,
                                            QStringLiteral("9.00kB/s"));
    factory.created.at(0)->sendFileProgress(QStringLiteral("folder/sub/b.txt"), 60,
                                            QStringLiteral("3.00kB/s"));
    QCOMPARE(fileByPath(model, 0, QStringLiteral("folder/sub/b.txt"))
                 .value(QStringLiteral("percent")).toInt(), 60);

    // No new rows were created by the live updates — they landed on the seeded rows.
    QCOMPARE(model->data(model->index(0), TransfersModel::FileCountRole).toInt(), 3);

    // A clean finish reconciles the untouched leaf to "up to date" rather than
    // leaving it stuck at 0%.
    factory.created.at(0)->finish(0);
    const QVariantMap c = fileByPath(model, 0, QStringLiteral("folder/sub/c.txt"));
    QCOMPARE(c.value(QStringLiteral("upToDate")).toBool(), true);
    QCOMPARE(c.value(QStringLiteral("percent")).toInt(), 100);

    // A transferred leaf is not flagged up-to-date.
    QCOMPARE(fileByPath(model, 0, QStringLiteral("folder/a.txt"))
                 .value(QStringLiteral("upToDate")).toBool(), false);
}

void TransferManagerTest::seedDoesNotResetLivePaths()
{
    FakeEngineFactory factory;
    TransferManager mgr(std::ref(factory));
    mgr.setMaxConcurrent(1);
    TransfersModel *model = mgr.model();

    const QString id = mgr.enqueue(jobN(1), QStringLiteral("up"), QStringLiteral("folder"));

    // The transfer outruns the walk: a live update creates a row at 50% first.
    factory.created.at(0)->sendFileProgress(QStringLiteral("folder/a.txt"), 50,
                                            QStringLiteral("5.00kB/s"));
    QCOMPARE(fileByPath(model, 0, QStringLiteral("folder/a.txt"))
                 .value(QStringLiteral("percent")).toInt(), 50);

    // Seeding arrives late and includes that same path — it must not reset it to 0%.
    mgr.seedFiles(id, {QStringLiteral("folder/a.txt"), QStringLiteral("folder/b.txt")});
    QCOMPARE(fileByPath(model, 0, QStringLiteral("folder/a.txt"))
                 .value(QStringLiteral("percent")).toInt(), 50);
    QCOMPARE(fileByPath(model, 0, QStringLiteral("folder/b.txt"))
                 .value(QStringLiteral("percent")).toInt(), 0);
    QCOMPARE(model->data(model->index(0), TransfersModel::FileCountRole).toInt(), 2);
}

void TransferManagerTest::retryResubmitsFailedTransfer()
{
    FakeEngineFactory factory;
    TransferManager mgr(std::ref(factory));
    mgr.setMaxConcurrent(1);
    TransfersModel *model = mgr.model();

    const QString id = mgr.enqueue(jobN(0), QStringLiteral("up"), QStringLiteral("t0"));
    factory.created.at(0)->finish(23);  // non-zero exit -> Failed
    QCOMPARE(countStatus(model, TransfersModel::Failed), 1);

    // Retrying re-runs the same row (a second engine starts) and it goes Active again.
    mgr.retry(id);
    QCOMPARE(factory.created.size(), 2);
    QCOMPARE(countStatus(model, TransfersModel::Active), 1);
    QCOMPARE(countStatus(model, TransfersModel::Failed), 0);

    // The resubmitted run can complete normally.
    factory.created.at(1)->finish(0);
    QCOMPARE(countStatus(model, TransfersModel::Done), 1);

    // A succeeded transfer drops its retained job: retrying it is a no-op.
    mgr.retry(id);
    QCOMPARE(factory.created.size(), 2);
}

void TransferManagerTest::allCompleteFiresWhenQueueDrains()
{
    FakeEngineFactory factory;
    TransferManager mgr(std::ref(factory));
    mgr.setMaxConcurrent(2);
    QSignalSpy spy(&mgr, &TransferManager::allTransfersComplete);

    mgr.enqueue(jobN(0), QStringLiteral("up"), QStringLiteral("t0"));
    mgr.enqueue(jobN(1), QStringLiteral("up"), QStringLiteral("t1"));

    // Finishing the first (of two active) doesn't drain the queue yet.
    factory.created.at(0)->finish(0);
    QCOMPARE(spy.count(), 0);

    // The second finishes with an error: now drained, signal fires once with failed=1.
    factory.created.at(1)->finish(23);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 1);
}

QTEST_MAIN(TransferManagerTest)
#include "tst_transfermanager.moc"
