#include "core/PairedDeviceStore.h"

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

PairedDeviceStore::PairedDeviceStore(QString path) : m_path(std::move(path)) {}

QString PairedDeviceStore::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/Ceres/paired-devices.json");
}

QJsonObject PairedDeviceStore::toJson(const PairedDevice &device)
{
    QJsonObject o;
    o[QStringLiteral("schema")] = kSchema;
    o[QStringLiteral("deviceId")] = device.deviceId;
    o[QStringLiteral("name")] = device.name;
    o[QStringLiteral("sshTarget")] = device.sshTarget;
    o[QStringLiteral("pairedAtMs")] = QString::number(device.pairedAtMs);  // qint64 via string
    return o;
}

PairedDevice PairedDeviceStore::fromJson(const QJsonObject &obj)
{
    PairedDevice d;
    d.deviceId = obj.value(QStringLiteral("deviceId")).toString().trimmed();
    d.name = obj.value(QStringLiteral("name")).toString();
    d.sshTarget = obj.value(QStringLiteral("sshTarget")).toString().trimmed();
    d.pairedAtMs = obj.value(QStringLiteral("pairedAtMs")).toString().toLongLong();
    return d;
}

QList<PairedDevice> PairedDeviceStore::loadAll() const
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray())
        return {};

    QList<PairedDevice> devices;
    for (const QJsonValue &value : doc.array()) {
        if (!value.isObject())
            continue;
        PairedDevice d = fromJson(value.toObject());
        if (!d.deviceId.isEmpty())
            devices << d;
    }
    return devices;
}

bool PairedDeviceStore::saveAll(const QList<PairedDevice> &devices) const
{
    QDir dir(QFileInfo(m_path).absolutePath());
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return false;

    QJsonArray array;
    for (const PairedDevice &d : devices) {
        if (!d.deviceId.isEmpty())
            array.append(toJson(d));
    }

    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    return file.commit();
}

bool PairedDeviceStore::upsert(const PairedDevice &device) const
{
    if (device.deviceId.isEmpty())
        return false;
    QList<PairedDevice> all = loadAll();
    bool replaced = false;
    for (PairedDevice &d : all) {
        if (d.deviceId == device.deviceId) {
            d = device;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        all << device;
    return saveAll(all);
}

bool PairedDeviceStore::remove(const QString &deviceId) const
{
    QList<PairedDevice> all = loadAll();
    const qsizetype before = all.size();
    all.removeIf([&](const PairedDevice &d) { return d.deviceId == deviceId; });
    return all.size() != before && saveAll(all);
}

PairedDevice PairedDeviceStore::load(const QString &deviceId) const
{
    for (const PairedDevice &d : loadAll()) {
        if (d.deviceId == deviceId)
            return d;
    }
    return {};
}

bool PairedDeviceStore::contains(const QString &deviceId) const
{
    return !load(deviceId).deviceId.isEmpty();
}
