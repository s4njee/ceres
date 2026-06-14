#include "core/ProfileStore.h"

#include <utility>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
constexpr int kSchema = 1;
}

ProfileStore::ProfileStore(QString directory) : m_dir(std::move(directory)) {}

QString ProfileStore::defaultDirectory()
{
    // GenericConfigLocation (no app segment) + an explicit "Ceres" avoids Qt's
    // doubled "Ceres/Ceres" when organizationName == applicationName. Resolves to
    // ~/Library/Preferences/Ceres/jobs (macOS) and ~/.config/Ceres/jobs (Linux).
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/Ceres/jobs");
}

QJsonObject ProfileStore::toJson(const SyncJob &job)
{
    QJsonObject o;
    o[QStringLiteral("schema")] = kSchema;
    o[QStringLiteral("id")] = job.id;
    o[QStringLiteral("name")] = job.name;
    o[QStringLiteral("source")] = job.source;
    o[QStringLiteral("destination")] = job.destination;
    o[QStringLiteral("archive")] = job.archive;
    o[QStringLiteral("compress")] = job.compress;
    o[QStringLiteral("delete")] = job.deleteExtraneous;
    o[QStringLiteral("checksum")] = job.checksum;
    o[QStringLiteral("excludes")] = QJsonArray::fromStringList(job.excludes);
    o[QStringLiteral("extraArgs")] = QJsonArray::fromStringList(job.extraArgs);
    o[QStringLiteral("sshKeyPath")] = job.sshKeyPath;
    o[QStringLiteral("sshPort")] = job.sshPort;
    o[QStringLiteral("schedule")] = scheduleKindToString(job.schedule);
    o[QStringLiteral("intervalMinutes")] = job.intervalMinutes;
    o[QStringLiteral("atHour")] = job.atHour;
    o[QStringLiteral("atMinute")] = job.atMinute;
    o[QStringLiteral("weekday")] = job.weekday;
    // daemonPassword is intentionally never persisted.
    return o;
}

SyncJob ProfileStore::fromJson(const QJsonObject &o)
{
    SyncJob j;
    j.id = o.value(QStringLiteral("id")).toString();
    j.name = o.value(QStringLiteral("name")).toString();
    j.source = o.value(QStringLiteral("source")).toString();
    j.destination = o.value(QStringLiteral("destination")).toString();
    j.archive = o.value(QStringLiteral("archive")).toBool(true);
    j.compress = o.value(QStringLiteral("compress")).toBool(false);
    j.deleteExtraneous = o.value(QStringLiteral("delete")).toBool(false);
    j.checksum = o.value(QStringLiteral("checksum")).toBool(false);
    for (const QJsonValue &v : o.value(QStringLiteral("excludes")).toArray())
        j.excludes << v.toString();
    for (const QJsonValue &v : o.value(QStringLiteral("extraArgs")).toArray())
        j.extraArgs << v.toString();
    j.sshKeyPath = o.value(QStringLiteral("sshKeyPath")).toString();
    j.sshPort = o.value(QStringLiteral("sshPort")).toInt();
    j.schedule = scheduleKindFromString(o.value(QStringLiteral("schedule")).toString());
    j.intervalMinutes = o.value(QStringLiteral("intervalMinutes")).toInt(60);
    j.atHour = o.value(QStringLiteral("atHour")).toInt(9);
    j.atMinute = o.value(QStringLiteral("atMinute")).toInt(0);
    j.weekday = o.value(QStringLiteral("weekday")).toInt(0);
    return j;
}

QList<SyncJob> ProfileStore::loadAll() const
{
    QList<SyncJob> jobs;
    QDir dir(m_dir);
    if (!dir.exists())
        return jobs;
    const auto files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &name : files) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject())
            continue;
        SyncJob job = fromJson(doc.object());
        if (!job.id.isEmpty())
            jobs << job;
    }
    return jobs;
}

SyncJob ProfileStore::load(const QString &id) const
{
    if (id.isEmpty())
        return {};
    QFile file(m_dir + QStringLiteral("/") + id + QStringLiteral(".json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return {};
    return fromJson(doc.object());
}

bool ProfileStore::save(const SyncJob &job) const
{
    if (job.id.isEmpty())
        return false;
    QDir().mkpath(m_dir);
    QSaveFile file(m_dir + QStringLiteral("/") + job.id + QStringLiteral(".json"));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(toJson(job)).toJson(QJsonDocument::Indented));
    return file.commit();
}

bool ProfileStore::remove(const QString &id) const
{
    if (id.isEmpty())
        return false;
    return QFile::remove(m_dir + QStringLiteral("/") + id + QStringLiteral(".json"));
}
