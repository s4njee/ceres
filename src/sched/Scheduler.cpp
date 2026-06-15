#include "sched/Scheduler.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

#include "core/JobId.h"

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

QString unitExecArg(QString s)
{
    s = unitSafe(s);
    s.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    s.replace(QLatin1Char('"'), QLatin1String("\\\""));
    return QStringLiteral("\"") + s + QStringLiteral("\"");
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
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath() + QStringLiteral("/ceres-runner.exe");
#else
    return QCoreApplication::applicationDirPath() + QStringLiteral("/ceres-runner");
#endif
}

QString Scheduler::labelFor(const QString &id)
{
    if (!isSafeJobId(id))
        return {};
    return QStringLiteral("com.ceres.job.") + id;
}

QString Scheduler::windowsTaskName(const QString &id)
{
    if (!isSafeJobId(id))
        return {};
    return QStringLiteral("Ceres\\") + id;  // tasks live in a \Ceres folder
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
                          "[Service]\nType=oneshot\nExecStart=%2 --job %3\n")
        .arg(unitSafe(job.name), unitExecArg(runnerPath), unitSafe(job.id));
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

QString Scheduler::windowsTaskXml(const SyncJob &job, const QString &runnerPath)
{
    static const char *const days[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                       "Thursday", "Friday", "Saturday"};
    const QString hh = QStringLiteral("%1").arg(qBound(0, job.atHour, 23), 2, 10, QChar('0'));
    const QString mm = QStringLiteral("%1").arg(qBound(0, job.atMinute, 59), 2, 10, QChar('0'));

    QString trigger;
    switch (job.schedule) {
    case ScheduleKind::Interval:
        // A repeating time trigger; Task Scheduler needs an ISO-8601 interval.
        trigger = QStringLiteral(
                      "    <TimeTrigger>\n"
                      "      <StartBoundary>2020-01-01T00:00:00</StartBoundary>\n"
                      "      <Enabled>true</Enabled>\n"
                      "      <Repetition>\n"
                      "        <Interval>PT%1M</Interval>\n"
                      "        <StopAtDurationEnd>false</StopAtDurationEnd>\n"
                      "      </Repetition>\n"
                      "    </TimeTrigger>\n")
                      .arg(qMax(1, job.intervalMinutes));
        break;
    case ScheduleKind::Daily:
        trigger = QStringLiteral(
                      "    <CalendarTrigger>\n"
                      "      <StartBoundary>2020-01-01T%1:%2:00</StartBoundary>\n"
                      "      <Enabled>true</Enabled>\n"
                      "      <ScheduleByDay>\n        <DaysInterval>1</DaysInterval>\n      </ScheduleByDay>\n"
                      "    </CalendarTrigger>\n")
                      .arg(hh, mm);
        break;
    case ScheduleKind::Weekly:
        trigger = QStringLiteral(
                      "    <CalendarTrigger>\n"
                      "      <StartBoundary>2020-01-01T%1:%2:00</StartBoundary>\n"
                      "      <Enabled>true</Enabled>\n"
                      "      <ScheduleByWeek>\n"
                      "        <DaysOfWeek>\n          <%3 />\n        </DaysOfWeek>\n"
                      "        <WeeksInterval>1</WeeksInterval>\n      </ScheduleByWeek>\n"
                      "    </CalendarTrigger>\n")
                      .arg(hh, mm, QLatin1String(days[qBound(0, job.weekday, 6)]));
        break;
    case ScheduleKind::Manual:
        break;
    }

    // Single multi-arg substitution so a '%' or '%N' in name/path can't be re-read.
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
               "<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n"
               "  <RegistrationInfo>\n    <Description>Ceres sync %1</Description>\n  </RegistrationInfo>\n"
               "  <Triggers>\n%2  </Triggers>\n"
               "  <Principals>\n    <Principal id=\"Author\">\n"
               "      <LogonType>InteractiveToken</LogonType>\n"
               "      <RunLevel>LeastPrivilege</RunLevel>\n    </Principal>\n  </Principals>\n"
               "  <Settings>\n"
               "    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\n"
               "    <StartWhenAvailable>true</StartWhenAvailable>\n"
               "    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\n"
               "    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\n"
               "    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\n"
               "  </Settings>\n"
               "  <Actions Context=\"Author\">\n    <Exec>\n"
               "      <Command>%3</Command>\n      <Arguments>--job %4</Arguments>\n"
               "    </Exec>\n  </Actions>\n"
               "</Task>\n")
        .arg(xmlEscape(job.name), trigger, xmlEscape(runnerPath), xmlEscape(job.id));
}

bool Scheduler::apply(const SyncJob &job, const QString &runnerPath) const
{
    if (!isSafeJobId(job.id))
        return false;
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
#elif defined(Q_OS_WIN)
    const QString xml = windowsTaskXml(job, runnerPath);
    const QString xmlPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/ceres-") + job.id + QStringLiteral(".xml");
    {
        // schtasks /Create /XML wants a UTF-16 file (with BOM).
        QSaveFile f(xmlPath);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        QByteArray bytes("\xFF\xFE", 2);  // UTF-16LE BOM
        bytes.append(reinterpret_cast<const char *>(xml.utf16()), xml.size() * 2);
        f.write(bytes);
        if (!f.commit())
            return false;
    }
    const int rc = QProcess::execute(
        QStringLiteral("schtasks"),
        {QStringLiteral("/Create"), QStringLiteral("/TN"), windowsTaskName(job.id),
         QStringLiteral("/XML"), xmlPath, QStringLiteral("/F")});
    QFile::remove(xmlPath);
    return rc == 0;
#else
    Q_UNUSED(runnerPath);
    return false;
#endif
}

bool Scheduler::remove(const QString &id) const
{
    if (!isSafeJobId(id))
        return false;
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
#elif defined(Q_OS_WIN)
    return QProcess::execute(
               QStringLiteral("schtasks"),
               {QStringLiteral("/Delete"), QStringLiteral("/TN"), windowsTaskName(id),
                QStringLiteral("/F")})
        == 0;
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
        if (f.startsWith(prefix)) {
            const QString id = f.mid(prefix.size());
            if (isSafeJobId(id))
                ids << id;
        }
    }
#elif defined(Q_OS_LINUX)
    const QString unitDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/systemd/user");
    const auto files = QDir(unitDir).entryList({QStringLiteral("ceres-*.timer")}, QDir::Files);
    const QString prefix = QStringLiteral("ceres-");
    for (QString f : files) {
        f.chop(6);  // ".timer"
        if (f.startsWith(prefix)) {
            const QString id = f.mid(prefix.size());
            if (isSafeJobId(id))
                ids << id;
        }
    }
#elif defined(Q_OS_WIN)
    QProcess p;
    p.start(QStringLiteral("schtasks"),
            {QStringLiteral("/Query"), QStringLiteral("/FO"), QStringLiteral("CSV"),
             QStringLiteral("/NH")});
    if (p.waitForStarted(2000) && p.waitForFinished(5000)) {
        const QString prefix = QStringLiteral("\\Ceres\\");
        const QString out = QString::fromLocal8Bit(p.readAllStandardOutput());
        const auto lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            // First CSV field is the quoted task path, e.g. "\Ceres\jobid".
            QString name = line.section(QLatin1Char(','), 0, 0).trimmed();
            if (name.size() >= 2 && name.startsWith(QLatin1Char('"')) && name.endsWith(QLatin1Char('"')))
                name = name.mid(1, name.size() - 2);
            if (name.startsWith(prefix)) {
                const QString id = name.mid(prefix.size());
                if (isSafeJobId(id))
                    ids << id;
            }
        }
    }
#endif
    return ids;
}

bool Scheduler::isRegistered(const QString &id) const
{
    if (!isSafeJobId(id))
        return false;
#if defined(Q_OS_MACOS)
    return QFile::exists(launchAgentsDir() + QStringLiteral("/") + labelFor(id) + QStringLiteral(".plist"));
#elif defined(Q_OS_LINUX)
    const QString unitDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/systemd/user");
    return QFile::exists(unitDir + QStringLiteral("/ceres-") + id + QStringLiteral(".timer"));
#elif defined(Q_OS_WIN)
    return QProcess::execute(
               QStringLiteral("schtasks"),
               {QStringLiteral("/Query"), QStringLiteral("/TN"), windowsTaskName(id)})
        == 0;
#else
    Q_UNUSED(id);
    return false;
#endif
}
