#pragma once

#include <QList>
#include <QString>

#include "core/PairedDevice.h"

class QJsonObject;

/// Persists paired mesh devices in one JSON file under the Ceres config dir, keyed by
/// stable `deviceId`. Mirrors SshHostStore's shape (JSON array, `defaultPath()`,
/// upsert/load/contains/remove). No secrets here — credentials still live in
/// SecretStore / ~/.ssh.
/// @ingroup core
class PairedDeviceStore {
public:
    explicit PairedDeviceStore(QString path = defaultPath());

    static QString defaultPath();
    QString path() const { return m_path; }

    QList<PairedDevice> loadAll() const;
    bool saveAll(const QList<PairedDevice> &devices) const;
    bool upsert(const PairedDevice &device) const;  // replace by deviceId, or append
    bool remove(const QString &deviceId) const;
    PairedDevice load(const QString &deviceId) const;
    bool contains(const QString &deviceId) const;

    static QJsonObject toJson(const PairedDevice &device);
    static PairedDevice fromJson(const QJsonObject &obj);

private:
    QString m_path;
};
