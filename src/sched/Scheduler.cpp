#include "sched/Scheduler.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

#ifdef Q_OS_MACOS
#  include <unistd.h>
#endif

namespace {
QString xmlEscape(QString s)
{
    s.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    s.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    s.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    return s;
}

// systemd unit files are line-oriented INI: strip CR/LF so a value can't inject
// new directives, and double '%' so it isn't read as a systemd specifier.
QString unitSafe(QString s)
{
    s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    s.replace(QLatin1Char('\r'), QLatin1Char(' '));
    s.replace(QLatin1Char('%'), QLatin1String("%%"));
    return s;
}

QString logDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/Ceres/logs");
}

QString launchAgentsDir()
{
    return QDir::homePath() + QStringLiteral("/Library/LaunchAgents");
}

bool writeFileAtomic(const QString &path, const QString &content)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(content.toUtf8());
    return f.commit();
}
} // namespace

QString Scheduler::runnerPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/ceres-runner");
}

QString Scheduler::labelFor(const QString &id)
{
    return QStringLiteral("com.ceres.job.") + id;
}

QString Scheduler::launchdPlist(const SyncJob &job, const QString &runnerPath, const QString &label)
{
    QString schedule;
    switch (job.schedule) {
    case ScheduleKind::Interval:
        schedule = QStringLiteral("  <key>StartInterval</key>\n  <integer>%1</integer>\n")
                       .arg(qMax(60, job.intervalMinutes * 60));
        break;
    case ScheduleKind::Daily:
        schedule = QStringLiteral("  <key>StartCalendarInterval</key>\n  <dict>\n"
                                  "    <key>Hour</key><integer>%1</integer>\n"
                                  "    <key>Minute</key><integer>%2</integer>\n  </dict>\n")
                       .arg(qBound(0, job.atHour, 23))
                       .arg(qBound(0, job.atMinute, 59));
        break;
    case ScheduleKind::Weekly:
        schedule = QStringLiteral("  <key>StartCalendarInterval</key>\n  <dict>\n"
                                  "    <key>Weekday</key><integer>%1</integer>\n"
                                  "    <key>Hour</key><integer>%2</integer>\n"
                                  "    <key>Minute</key><integer>%3</integer>\n  </dict>\n")
                       .arg(qBound(0, job.weekday, 6))
                       .arg(qBound(0, job.atHour, 23))
                       .arg(qBound(0, job.atMinute, 59));
        break;
    case ScheduleKind::Manual:
        break;
    }

    const QString log = logDir() + QStringLiteral("/") + job.id + QStringLiteral(".log");

    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
               "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
               "<plist version=\"1.0\">\n<dict>\n"
               "  <key>Label</key>\n  <string>%1</string>\n"
               "  <key>ProgramArguments</key>\n  <array>\n"
               "    <string>%2</string>\n    <string>--job</string>\n    <string>%3</string>\n"
               "  </array>\n"
               "  <key>RunAtLoad</key>\n  <false/>\n"
               "%4"
               "  <key>StandardOutPath</key>\n  <string>%5</string>\n"
               "  <key>StandardErrorPath</key>\n  <string>%5</string>\n"
               "</dict>\n</plist>\n")
        .arg(xmlEscape(label))
        .arg(xmlEscape(runnerPath))
        .arg(xmlEscape(job.id))
        .arg(schedule)
        .arg(xmlEscape(log));
}

QString Scheduler::systemdService(const SyncJob &job, const QString &runnerPath)
{
    // Quote the runner path so an install dir with spaces (e.g. AppImage mounts)
    // is treated as a single argument by systemd.
    return QStringLiteral("[Unit]\nDescription=Ceres sync %1\n\n"
                          "[Service]\nType=oneshot\nExecStart=\"%2\" --job %3\n")
        .arg(unitSafe(job.name), runnerPath, job.id);
}

QString Scheduler::systemdTimer(const SyncJob &job)
{
    static const char *const days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    QString onClause;
    switch (job.schedule) {
    case ScheduleKind::Interval:
        onClause = QStringLiteral("OnUnitActiveSec=%1min\nOnBootSec=%1min\n")
                       .arg(qMax(1, job.intervalMinutes));
        break;
    case ScheduleKind::Daily:
        onClause = QStringLiteral("OnCalendar=*-*-* %1:%2:00\n")
                       .arg(qBound(0, job.atHour, 23), 2, 10, QChar('0'))
                       .arg(qBound(0, job.atMinute, 59), 2, 10, QChar('0'));
        break;
    case ScheduleKind::Weekly:
        onClause = QStringLiteral("OnCalendar=%1 *-*-* %2:%3:00\n")
                       .arg(QLatin1String(days[qBound(0, job.weekday, 6)]))
                       .arg(qBound(0, job.atHour, 23), 2, 10, QChar('0'))
                       .arg(qBound(0, job.atMinute, 59), 2, 10, QChar('0'));
        break;
    case ScheduleKind::Manual:
        break;
    }
    return QStringLiteral("[Unit]\nDescription=Ceres sync timer %1\n\n[Timer]\n%2Persistent=true\n\n"
                          "[Install]\nWantedBy=timers.target\n")
        .arg(unitSafe(job.name), onClause);
}

bool Scheduler::apply(const SyncJob &job, const QString &runnerPath) const
{
    if (job.schedule == ScheduleKind::Manual)
        return remove(job.id);

#if defined(Q_OS_MACOS)
    QDir().mkpath(launchAgentsDir());
    QDir().mkpath(logDir());
    const QString label = labelFor(job.id);
    const QString plistPath = launchAgentsDir() + QStringLiteral("/") + label + QStringLiteral(".plist");
    if (!writeFileAtomic(plistPath, launchdPlist(job, runnerPath, label)))
        return false;
    const QString domain = QStringLiteral("gui/%1").arg(getuid());
    QProcess::execute(QStringLiteral("launchctl"), {QStringLiteral("bootout"), domain + QStringLiteral("/") + label});
    return QProcess::execute(QStringLiteral("launchctl"), {QStringLiteral("bootstrap"), domain, plistPath}) == 0;
#elif defined(Q_OS_LINUX)
    const QString unitDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/systemd/user");
    QDir().mkpath(unitDir);
    const QString base = QStringLiteral("ceres-") + job.id;
    if (!writeFileAtomic(unitDir + QStringLiteral("/") + base + QStringLiteral(".service"),
                         systemdService(job, runnerPath)))
        return false;
    if (!writeFileAtomic(unitDir + QStringLiteral("/") + base + QStringLiteral(".timer"),
                         systemdTimer(job)))
        return false;
    QProcess::execute(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("daemon-reload")});
    return QProcess::execute(QStringLiteral("systemctl"),
                             {QStringLiteral("--user"), QStringLiteral("enable"),
                              QStringLiteral("--now"), base + QStringLiteral(".timer")})
        == 0;
#else
    Q_UNUSED(runnerPath);
    return false;
#endif
}

bool Scheduler::remove(const QString &id) const
{
#if defined(Q_OS_MACOS)
    const QString label = labelFor(id);
    const QString domain = QStringLiteral("gui/%1").arg(getuid());
    const QString plistPath = launchAgentsDir() + QStringLiteral("/") + label + QStringLiteral(".plist");
    // Delete the plist BEFORE bootout: bootout may terminate the caller (the
    // runner self-removing), so the file must already be gone to prevent reload.
    QFile::remove(plistPath);
    QProcess::execute(QStringLiteral("launchctl"), {QStringLiteral("bootout"), domain + QStringLiteral("/") + label});
    return !QFile::exists(plistPath);  // success = the unit is gone (whoever removed it)
#elif defined(Q_OS_LINUX)
    const QString unitDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/systemd/user");
    const QString base = QStringLiteral("ceres-") + id;
    QProcess::execute(QStringLiteral("systemctl"),
                      {QStringLiteral("--user"), QStringLiteral("disable"), QStringLiteral("--now"),
                       base + QStringLiteral(".timer")});
    QFile::remove(unitDir + QStringLiteral("/") + base + QStringLiteral(".timer"));
    QFile::remove(unitDir + QStringLiteral("/") + base + QStringLiteral(".service"));
    QProcess::execute(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("daemon-reload")});
    return true;
#else
    Q_UNUSED(id);
    return false;
#endif
}

QStringList Scheduler::installedJobIds() const
{
    QStringList ids;
#if defined(Q_OS_MACOS)
    const QString prefix = QStringLiteral("com.ceres.job.");
    const auto files = QDir(launchAgentsDir())
                           .entryList({prefix + QStringLiteral("*.plist")}, QDir::Files);
    for (QString f : files) {
        f.chop(6);  // ".plist"
        if (f.startsWith(prefix))
            ids << f.mid(prefix.size());
    }
#elif defined(Q_OS_LINUX)
    const QString unitDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/systemd/user");
    const auto files = QDir(unitDir).entryList({QStringLiteral("ceres-*.timer")}, QDir::Files);
    const QString prefix = QStringLiteral("ceres-");
    for (QString f : files) {
        f.chop(6);  // ".timer"
        if (f.startsWith(prefix))
            ids << f.mid(prefix.size());
    }
#endif
    return ids;
}

bool Scheduler::isRegistered(const QString &id) const
{
#if defined(Q_OS_MACOS)
    return QFile::exists(launchAgentsDir() + QStringLiteral("/") + labelFor(id) + QStringLiteral(".plist"));
#elif defined(Q_OS_LINUX)
    const QString unitDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/systemd/user");
    return QFile::exists(unitDir + QStringLiteral("/ceres-") + id + QStringLiteral(".timer"));
#else
    Q_UNUSED(id);
    return false;
#endif
}
