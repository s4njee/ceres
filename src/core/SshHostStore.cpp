#include "core/SshHostStore.h"

#include <utility>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
constexpr int kSchema = 1;
}

SshHostStore::SshHostStore(QString path) : m_path(std::move(path)) {}

QString SshHostStore::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/Ceres/ssh-hosts.json");
}

QJsonObject SshHostStore::toJson(const SshHost &host)
{
    QJsonObject o;
    o[QStringLiteral("schema")] = kSchema;
    o[QStringLiteral("target")] = host.target;
    o[QStringLiteral("host")] = host.host;
    o[QStringLiteral("user")] = host.user;
    o[QStringLiteral("label")] = host.label;
    o[QStringLiteral("sshKeyPath")] = host.sshKeyPath;
    o[QStringLiteral("sshPort")] = host.sshPort;
    o[QStringLiteral("hasPassword")] = host.hasPassword;
    return o;
}

SshHost SshHostStore::fromJson(const QJsonObject &obj)
{
    SshHost host;
    host.target = obj.value(QStringLiteral("target")).toString().trimmed();
    host.host = obj.value(QStringLiteral("host")).toString().trimmed();
    host.user = obj.value(QStringLiteral("user")).toString().trimmed();
    host.label = obj.value(QStringLiteral("label")).toString().trimmed();
    host.sshKeyPath = obj.value(QStringLiteral("sshKeyPath")).toString();
    host.sshPort = qBound(0, obj.value(QStringLiteral("sshPort")).toInt(), 65535);
    host.hasPassword = obj.value(QStringLiteral("hasPassword")).toBool(false);

    if (host.host.isEmpty()) {
        const int at = host.target.lastIndexOf(QLatin1Char('@'));
        host.user = at >= 0 ? host.target.left(at) : host.user;
        host.host = at >= 0 ? host.target.mid(at + 1) : host.target;
    }
    if (host.label.isEmpty())
        host.label = host.target;
    return host;
}

QList<SshHost> SshHostStore::loadAll() const
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray())
        return {};

    QList<SshHost> hosts;
    for (const QJsonValue &value : doc.array()) {
        if (!value.isObject())
            continue;
        SshHost host = fromJson(value.toObject());
        if (!host.target.isEmpty() && !host.host.isEmpty())
            hosts << host;
    }
    return hosts;
}

bool SshHostStore::saveAll(const QList<SshHost> &hosts) const
{
    QDir dir(QFileInfo(m_path).absolutePath());
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return false;

    QJsonArray array;
    for (const SshHost &host : hosts) {
        if (!host.target.isEmpty() && !host.host.isEmpty())
            array.append(toJson(host));
    }

    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    return file.commit();
}

bool SshHostStore::upsert(const SshHost &host) const
{
    if (host.target.isEmpty() || host.host.isEmpty())
        return false;

    QList<SshHost> hosts = loadAll();
    for (SshHost &existing : hosts) {
        if (existing.target == host.target) {
            existing = host;
            return saveAll(hosts);
        }
    }
    hosts << host;
    return saveAll(hosts);
}

SshHost SshHostStore::load(const QString &target) const
{
    for (const SshHost &host : loadAll()) {
        if (host.target == target)
            return host;
    }
    return {};
}

bool SshHostStore::contains(const QString &target) const
{
    return !load(target).target.isEmpty();
}
