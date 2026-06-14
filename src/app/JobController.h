#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

#include "core/ProfileStore.h"
#include "core/SecretStore.h"
#include "engine/BinaryLocator.h"
#include "models/ChangeListModel.h"
#include "models/JobListModel.h"
#include "models/PeerModel.h"
#include "sched/Scheduler.h"

class RsyncProcessEngine;
class DiscoveryService;

// The single object QML talks to. Owns the engine + change model, exposes a
// dry-run preview, and surfaces progress / log / status as bindable properties.
class JobController : public QObject {
    Q_OBJECT
    Q_PROPERTY(ChangeListModel *changes READ changes CONSTANT)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString rsyncSummary READ rsyncSummary CONSTANT)
    Q_PROPERTY(bool usingOpenRsync READ usingOpenRsync CONSTANT)
    Q_PROPERTY(QString log READ log NOTIFY logChanged)
    Q_PROPERTY(int percent READ percent NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString hostName READ hostName CONSTANT)
    Q_PROPERTY(QString hostAddress READ hostAddress CONSTANT)
    Q_PROPERTY(JobListModel *jobs READ jobs CONSTANT)
    Q_PROPERTY(QString currentId READ currentId NOTIFY currentChanged)
    Q_PROPERTY(PeerModel *peers READ peers CONSTANT)
    Q_PROPERTY(bool discoverable READ discoverable WRITE setDiscoverable NOTIFY discoverableChanged)
public:
    explicit JobController(QObject *parent = nullptr);

    ChangeListModel *changes() { return &m_changes; }
    JobListModel *jobs() { return &m_jobs; }
    QString currentId() const { return m_currentId; }
    PeerModel *peers() { return &m_peers; }
    bool discoverable() const { return m_discoverable; }
    void setDiscoverable(bool on);
    bool running() const { return m_running; }
    bool usingOpenRsync() const { return m_caps.isOpenRsync; }
    QString rsyncSummary() const;
    QString log() const { return m_log; }
    int percent() const { return m_percent; }
    QString status() const { return m_status; }
    QString hostName() const { return m_hostName; }
    QString hostAddress() const { return m_hostAddress; }

    // The job map carries: name, source, destination, archive, compress,
    // deleteExtras, checksum, sshKey, sshPort, daemonPassword.
    Q_INVOKABLE void preview(const QVariantMap &job);  // dry-run
    Q_INVOKABLE void run(const QVariantMap &job);      // real sync (UI gates deletes)
    Q_INVOKABLE void cancel();

    // Profile management (sidebar jobs).
    Q_INVOKABLE void newJob();  // reset the editor to a blank, unsaved job
    Q_INVOKABLE void loadJob(const QString &id);
    Q_INVOKABLE void saveJob(const QVariantMap &job);
    Q_INVOKABLE void deleteJob(const QString &id);

    // Discovery.
    Q_INVOKABLE void addPeerByHost(const QString &host);

signals:
    void runningChanged();
    void logChanged();
    void progressChanged();
    void statusChanged();
    void currentChanged();
    void discoverableChanged();
    // Pushes a job's fields into the editor (new job -> all blank, archive on).
    void jobLoaded(const QVariantMap &job);

private:
    void startJob(const SyncJob &job, bool dryRun);
    SyncJob jobFromMap(const QVariantMap &map) const;
    QVariantMap mapFromJob(const SyncJob &job) const;
    void setRunning(bool running);
    void setStatus(const QString &status);
    void appendLog(const QString &line);

    RsyncCapabilities m_caps;
    RsyncProcessEngine *m_engine = nullptr;
    ChangeListModel m_changes;
    ProfileStore m_store;
    SecretStore m_secrets;
    JobListModel m_jobs;
    QString m_currentId;
    PeerModel m_peers;
    DiscoveryService *m_discovery = nullptr;
    bool m_discoverable = true;
    Scheduler m_scheduler;

    QString m_log;
    QString m_status;
    QString m_hostName;
    QString m_hostAddress;
    int m_percent = 0;
    bool m_running = false;
    bool m_activeDryRun = true;
};
