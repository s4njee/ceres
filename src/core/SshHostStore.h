#pragma once

#include <QList>
#include <QString>

#include "core/SshHost.h"

class QJsonObject;

/// Persists saved SSH hosts in one JSON file under the Ceres config directory.
///
/// Secrets are stored separately in SecretStore. This store keeps only target
/// metadata and a non-secret hasPassword flag so the sidebar can show hosts that
/// were saved from password auth even before a full sync profile exists.
/// @ingroup core
class SshHostStore {
public:
    explicit SshHostStore(QString path = defaultPath());

    static QString defaultPath();
    QString path() const { return m_path; }

    QList<SshHost> loadAll() const;
    bool saveAll(const QList<SshHost> &hosts) const;
    bool upsert(const SshHost &host) const;
    SshHost load(const QString &target) const;
    bool contains(const QString &target) const;

    static QJsonObject toJson(const SshHost &host);
    static SshHost fromJson(const QJsonObject &obj);

private:
    QString m_path;
};
