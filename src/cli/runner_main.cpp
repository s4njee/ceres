#include <QCoreApplication>
#include <QTextStream>

#include "core/ProfileStore.h"
#include "core/SecretStore.h"
#include "core/SyncJob.h"
#include "engine/BinaryLocator.h"
#include "engine/RsyncProcessEngine.h"
#include "sched/Scheduler.h"

namespace {
bool isDaemonTarget(const SyncJob &j)
{
    const auto daemon = [](const QString &p) {
        return p.startsWith(QLatin1String("rsync://")) || p.contains(QLatin1String("::"));
    };
    return daemon(j.source) || daemon(j.destination);
}
}

// Headless runner for a saved job — the entry point the OS scheduler invokes:
//   ceres-runner --job <id>
// Reads the same profiles the GUI writes and runs a real (non-dry-run) sync.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Ceres"));
    QCoreApplication::setOrganizationName(QStringLiteral("Ceres"));

    QTextStream out(stdout);
    QTextStream err(stderr);

    QString jobId;
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--job") && i + 1 < args.size())
            jobId = args.at(++i);
    }
    if (jobId.isEmpty()) {
        err << "usage: ceres-runner --job <id>\n";
        return 64;
    }

    const ProfileStore store;
    const SyncJob job = store.load(jobId);
    if (job.id.isEmpty()) {
        // The job was deleted out from under a registered schedule — unregister
        // ourselves so this orphaned unit stops firing.
        err << "ceres-runner: job not found: " << jobId << " — removing its schedule\n";
        Scheduler().remove(jobId);
        return 65;
    }

    const RsyncCapabilities caps = BinaryLocator::locateRsync();
    if (!caps.found) {
        err << "ceres-runner: no rsync binary found\n";
        return 69;
    }

    SyncJob runJob = job;
    if (isDaemonTarget(runJob))  // daemon password lives in the keychain, not the profile
        runJob.daemonPassword = SecretStore().get(runJob.id);

    out << "ceres-runner: '" << job.name << "'  " << job.source << " -> " << job.destination << "\n";
    out.flush();

    RsyncProcessEngine engine(caps);
    QObject::connect(&engine, &SyncEngine::stats, [&](const QString &l) { out << l << "\n"; out.flush(); });
    QObject::connect(&engine, &SyncEngine::errorOutput, [&](const QString &l) { err << l << "\n"; err.flush(); });
    QObject::connect(&engine, &SyncEngine::progress, [&](const ProgressInfo &p) {
        out << "\r" << p.percent << "%  " << p.rate << "        ";
        out.flush();
    });
    QObject::connect(&engine, &SyncEngine::failedToStart, [&](const QString &reason) {
        err << "ceres-runner: failed to start: " << reason << "\n";
        QCoreApplication::exit(70);
    });
    QObject::connect(&engine, &SyncEngine::finished, [&](int code, bool crashed) {
        out << "\nceres-runner: finished (code " << code << (crashed ? ", crashed" : "") << ")\n";
        out.flush();
        QCoreApplication::exit(code);
    });

    engine.start(runJob, /*dryRun=*/false);
    return app.exec();
}
