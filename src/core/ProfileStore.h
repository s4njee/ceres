#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "core/SyncJob.h"

class QJsonObject;

// Persists sync profiles as one JSON file per job under AppConfigLocation/jobs.
// The directory is injectable so the (pure) serialization and the on-disk
// round-trip can both be unit-tested without touching the real config dir.
class ProfileStore {
public:
    explicit ProfileStore(QString directory = defaultDirectory());

    static QString defaultDirectory();
    QString directory() const { return m_dir; }

    QList<SyncJob> loadAll() const;
    // Ids of every profile file on disk, regardless of whether it parses — so
    // callers can tell "deleted" from "temporarily unreadable".
    QStringList presentJobIds() const;
    SyncJob load(const QString &id) const;  // single job, empty id if missing
    bool save(const SyncJob &job) const;    // atomic write of <id>.json
    bool remove(const QString &id) const;

    static QJsonObject toJson(const SyncJob &job);
    static SyncJob fromJson(const QJsonObject &obj);

private:
    QString m_dir;
};
