#include <QtTest>

#include "core/SyncJob.h"
#include "sched/Scheduler.h"

class SchedulerTest : public QObject {
    Q_OBJECT
private slots:
    void intervalPlist();
    void dailyPlist();
    void weeklyPlist();
    void plistEscapesRunnerPath();
    void systemdDailyTimer();
    void systemdSanitizesName();
    void systemdQuotesRunnerPath();
    void launchdClampsOutOfRangeTime();
    void rejectsUnsafeIds();
    void windowsIntervalTask();
    void windowsDailyTask();
    void windowsWeeklyTask();
    void windowsTaskEscapesAndClamps();
    void windowsTaskNameSafe();
};

void SchedulerTest::intervalPlist()
{
    SyncJob j;
    j.id = QStringLiteral("job1");
    j.schedule = ScheduleKind::Interval;
    j.intervalMinutes = 30;

    const QString p = Scheduler::launchdPlist(j, QStringLiteral("/Apps/ceres-runner"),
                                              Scheduler::labelFor(j.id));
    QVERIFY(p.contains(QStringLiteral("<key>Label</key>")));
    QVERIFY(p.contains(QStringLiteral("com.ceres.job.job1")));
    QVERIFY(p.contains(QStringLiteral("/Apps/ceres-runner")));
    QVERIFY(p.contains(QStringLiteral("<string>--job</string>")));
    QVERIFY(p.contains(QStringLiteral("<string>job1</string>")));
    QVERIFY(p.contains(QStringLiteral("<key>StartInterval</key>")));
    QVERIFY(p.contains(QStringLiteral("<integer>1800</integer>")));  // 30 * 60
    QVERIFY(!p.contains(QStringLiteral("StartCalendarInterval")));
}

void SchedulerTest::dailyPlist()
{
    SyncJob j;
    j.id = QStringLiteral("job2");
    j.schedule = ScheduleKind::Daily;
    j.atHour = 9;
    j.atMinute = 15;

    const QString p = Scheduler::launchdPlist(j, QStringLiteral("/r"), Scheduler::labelFor(j.id));
    QVERIFY(p.contains(QStringLiteral("<key>StartCalendarInterval</key>")));
    QVERIFY(p.contains(QStringLiteral("<key>Hour</key><integer>9</integer>")));
    QVERIFY(p.contains(QStringLiteral("<key>Minute</key><integer>15</integer>")));
    QVERIFY(!p.contains(QStringLiteral("Weekday")));
    QVERIFY(!p.contains(QStringLiteral("StartInterval</key>")));
}

void SchedulerTest::weeklyPlist()
{
    SyncJob j;
    j.id = QStringLiteral("job3");
    j.schedule = ScheduleKind::Weekly;
    j.weekday = 2;
    j.atHour = 6;
    j.atMinute = 0;

    const QString p = Scheduler::launchdPlist(j, QStringLiteral("/r"), Scheduler::labelFor(j.id));
    QVERIFY(p.contains(QStringLiteral("<key>Weekday</key><integer>2</integer>")));
    QVERIFY(p.contains(QStringLiteral("<key>Hour</key><integer>6</integer>")));
}

void SchedulerTest::plistEscapesRunnerPath()
{
    SyncJob j;
    j.id = QStringLiteral("job4");
    j.schedule = ScheduleKind::Interval;

    const QString p = Scheduler::launchdPlist(j, QStringLiteral("/Apps/A & B/ceres-runner"),
                                              Scheduler::labelFor(j.id));
    QVERIFY(p.contains(QStringLiteral("/Apps/A &amp; B/ceres-runner")));
    QVERIFY(!p.contains(QStringLiteral("A & B")));  // raw ampersand would be invalid XML
}

void SchedulerTest::systemdDailyTimer()
{
    SyncJob j;
    j.schedule = ScheduleKind::Daily;
    j.atHour = 7;
    j.atMinute = 5;

    const QString t = Scheduler::systemdTimer(j);
    QVERIFY(t.contains(QStringLiteral("OnCalendar=*-*-* 07:05:00")));
    QVERIFY(t.contains(QStringLiteral("Persistent=true")));
    QVERIFY(t.contains(QStringLiteral("WantedBy=timers.target")));
}

void SchedulerTest::systemdSanitizesName()
{
    SyncJob j;
    j.id = QStringLiteral("job5");
    j.name = QStringLiteral("evil\n[Service]\nExecStartPre=/bin/sh -c rm-rf");  // injection attempt
    j.schedule = ScheduleKind::Daily;

    // Newlines must be neutralised so the name can't START a new directive or a
    // second section — even though the literal text may survive inside Description=.
    const auto offending = [](const QString &unit) {
        int directiveLines = 0;
        int serviceHeaders = 0;
        for (const QString &line : unit.split(QLatin1Char('\n'))) {
            const QString t = line.trimmed();
            if (t.startsWith(QStringLiteral("ExecStartPre")))
                ++directiveLines;
            if (t == QStringLiteral("[Service]"))
                ++serviceHeaders;
        }
        return QPair<int, int>{directiveLines, serviceHeaders};
    };
    QCOMPARE(offending(Scheduler::systemdService(j, QStringLiteral("/r"))), (QPair<int, int>{0, 1}));
    QCOMPARE(offending(Scheduler::systemdTimer(j)), (QPair<int, int>{0, 0}));

    SyncJob pct;
    pct.id = QStringLiteral("job6");
    pct.name = QStringLiteral("100% backup");  // % must be doubled (systemd specifier)
    pct.schedule = ScheduleKind::Daily;
    QVERIFY(Scheduler::systemdService(pct, QStringLiteral("/r")).contains(QStringLiteral("100%% backup")));
}

void SchedulerTest::systemdQuotesRunnerPath()
{
    SyncJob j;
    j.id = QStringLiteral("job7");
    j.schedule = ScheduleKind::Daily;
    const QString svc = Scheduler::systemdService(j, QStringLiteral("/opt/My Apps/ceres-runner"));
    QVERIFY(svc.contains(QStringLiteral("ExecStart=\"/opt/My Apps/ceres-runner\" --job job7")));

    const QString quoted = Scheduler::systemdService(j, QStringLiteral("/opt/My \"Apps\"/ceres-runner"));
    QVERIFY(quoted.contains(QStringLiteral("ExecStart=\"/opt/My \\\"Apps\\\"/ceres-runner\" --job job7")));
}

void SchedulerTest::launchdClampsOutOfRangeTime()
{
    SyncJob j;
    j.id = QStringLiteral("job8");
    j.schedule = ScheduleKind::Daily;
    j.atHour = 40;    // out of range (free-form field)
    j.atMinute = 99;

    const QString p = Scheduler::launchdPlist(j, QStringLiteral("/r"), Scheduler::labelFor(j.id));
    QVERIFY(p.contains(QStringLiteral("<key>Hour</key><integer>23</integer>")));
    QVERIFY(p.contains(QStringLiteral("<key>Minute</key><integer>59</integer>")));
}

void SchedulerTest::rejectsUnsafeIds()
{
    QVERIFY(Scheduler::labelFor(QStringLiteral("../bad")).isEmpty());
    QVERIFY(!Scheduler().isRegistered(QStringLiteral("../bad")));
    QVERIFY(!Scheduler().remove(QStringLiteral("../bad")));

    SyncJob j;
    j.id = QStringLiteral("../bad");
    j.schedule = ScheduleKind::Daily;
    QVERIFY(!Scheduler().apply(j, QStringLiteral("/r")));
}

void SchedulerTest::windowsIntervalTask()
{
    SyncJob j;
    j.id = QStringLiteral("job1");
    j.name = QStringLiteral("My backup");
    j.schedule = ScheduleKind::Interval;
    j.intervalMinutes = 30;

    const QString x = Scheduler::windowsTaskXml(j, QStringLiteral("C:/Apps/ceres-runner.exe"));
    QVERIFY(x.contains(QStringLiteral("encoding=\"UTF-16\"")));
    QVERIFY(x.contains(QStringLiteral("<TimeTrigger>")));
    QVERIFY(x.contains(QStringLiteral("<Interval>PT30M</Interval>")));
    QVERIFY(x.contains(QStringLiteral("<Command>C:/Apps/ceres-runner.exe</Command>")));
    QVERIFY(x.contains(QStringLiteral("<Arguments>--job job1</Arguments>")));
    QVERIFY(!x.contains(QStringLiteral("CalendarTrigger")));
}

void SchedulerTest::windowsDailyTask()
{
    SyncJob j;
    j.id = QStringLiteral("job2");
    j.schedule = ScheduleKind::Daily;
    j.atHour = 9;
    j.atMinute = 15;

    const QString x = Scheduler::windowsTaskXml(j, QStringLiteral("/r"));
    QVERIFY(x.contains(QStringLiteral("<CalendarTrigger>")));
    QVERIFY(x.contains(QStringLiteral("<StartBoundary>2020-01-01T09:15:00</StartBoundary>")));
    QVERIFY(x.contains(QStringLiteral("<ScheduleByDay>")));
    QVERIFY(!x.contains(QStringLiteral("TimeTrigger")));
}

void SchedulerTest::windowsWeeklyTask()
{
    SyncJob j;
    j.id = QStringLiteral("job3");
    j.schedule = ScheduleKind::Weekly;
    j.weekday = 1;  // Monday
    j.atHour = 6;
    j.atMinute = 0;

    const QString x = Scheduler::windowsTaskXml(j, QStringLiteral("/r"));
    QVERIFY(x.contains(QStringLiteral("<ScheduleByWeek>")));
    QVERIFY(x.contains(QStringLiteral("<Monday />")));
    QVERIFY(x.contains(QStringLiteral("<StartBoundary>2020-01-01T06:00:00</StartBoundary>")));
}

void SchedulerTest::windowsTaskEscapesAndClamps()
{
    SyncJob j;
    j.id = QStringLiteral("job4");
    j.name = QStringLiteral("A & B <evil>");  // must be XML-escaped
    j.schedule = ScheduleKind::Daily;
    j.atHour = 40;     // out of range
    j.atMinute = 99;

    const QString x = Scheduler::windowsTaskXml(j, QStringLiteral("C:/A & B/ceres-runner.exe"));
    QVERIFY(x.contains(QStringLiteral("Ceres sync A &amp; B &lt;evil&gt;")));
    QVERIFY(x.contains(QStringLiteral("C:/A &amp; B/ceres-runner.exe")));
    QVERIFY(!x.contains(QStringLiteral("A & B")));  // raw ampersand would be invalid XML
    QVERIFY(x.contains(QStringLiteral("2020-01-01T23:59:00")));  // clamped time
}

void SchedulerTest::windowsTaskNameSafe()
{
    QCOMPARE(Scheduler::windowsTaskName(QStringLiteral("job1")), QStringLiteral("Ceres\\job1"));
    QVERIFY(Scheduler::windowsTaskName(QStringLiteral("../bad")).isEmpty());
}

QTEST_MAIN(SchedulerTest)
#include "tst_scheduler.moc"
