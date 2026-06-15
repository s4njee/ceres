#pragma once

#include <QObject>
#include <QByteArray>
#include <QStringList>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include "core/ProfileStore.h"
#include "core/SecretStore.h"
#include "engine/BinaryLocator.h"
#include "models/ChangeListModel.h"
#include "models/JobListModel.h"
#include "models/PeerModel.h"
#include "sched/Scheduler.h"

class DiscoveryService;
class SyncEngine;

/// The central coordinator between the QML UI and the C++ backend.
///
/// This is the single QObject that QML talks to — it's injected as a context
/// property named "controller" in main.cpp. It owns:
///   - The SyncEngine (runs rsync via QProcess)
///   - The ChangeListModel (preview / itemized changes list)
///   - The ProfileStore + SecretStore (persistence layer)
///   - The JobListModel (sidebar job list)
///   - The PeerModel + DiscoveryService (LAN peer discovery)
///   - The Scheduler (OS-level timers for background syncs)
///
/// **Dependency injection**: The two-argument constructor accepts all
/// dependencies (engine, stores, scheduler, capabilities) so unit tests can
/// inject mocks/stubs without needing a real rsync binary or filesystem.
/// The default constructor is used by the real app and auto-locates rsync.
///
/// **Destructive-run safety gate**: When `deleteExtraneous` is enabled,
/// the controller requires a matching preview (dry-run) fingerprint before
/// allowing a real sync. This prevents accidental mass-deletions if the user
/// changes source/destination after previewing. The fingerprint is a SHA-256
/// hash of the job's sync-relevant fields (see `syncFingerprint()`).
/// @ingroup app
class JobController : public QObject {
    Q_OBJECT
    Q_PROPERTY(ChangeListModel *changes READ changes CONSTANT)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString rsyncSummary READ rsyncSummary CONSTANT)
    Q_PROPERTY(bool usingOpenRsync READ usingOpenRsync CONSTANT)
    Q_PROPERTY(QString log READ log NOTIFY logChanged)
    Q_PROPERTY(int percent READ percent NOTIFY progressChanged)
    Q_PROPERTY(QString speed READ speed NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString hostName READ hostName CONSTANT)
    Q_PROPERTY(QString hostAddress READ hostAddress NOTIFY hostAddressChanged)
    Q_PROPERTY(JobListModel *jobs READ jobs CONSTANT)
    Q_PROPERTY(QString currentId READ currentId NOTIFY currentChanged)
    Q_PROPERTY(PeerModel *peers READ peers CONSTANT)
    Q_PROPERTY(bool discoverable READ discoverable WRITE setDiscoverable NOTIFY discoverableChanged)
public:
    /// Default constructor: auto-locates rsync and starts network services.
    /// Used by the real GUI application.
    explicit JobController(QObject *parent = nullptr);

    /// Test-friendly constructor: accepts all dependencies so tests can inject
    /// a mock SyncEngine, in-memory stores, etc. Set startNetworkServices=false
    /// to skip UDP beacon setup in headless test environments.
    JobController(RsyncCapabilities caps, SyncEngine *engine, ProfileStore store,
                  SecretStore secrets, Scheduler scheduler, bool startNetworkServices,
                  QObject *parent = nullptr);

    ChangeListModel *changes() { return &m_changes; }
    JobListModel *jobs() { return &m_jobs; }
    QString currentId() const { return m_currentId; }
    PeerModel *peers() { return &m_peers; }
    bool discoverable() const { return m_discoverable; }
    void setDiscoverable(bool on);
    bool running() const { return m_running; }
    bool usingOpenRsync() const { return m_caps.isOpenRsync; }
    QString rsyncSummary() const;

    /// Called from QML to classify a typed path as "local", "ssh", or "daemon".
    /// Drives conditional visibility of SSH key / daemon password fields.
    Q_INVOKABLE QString endpointKind(const QString &text) const;
    QString log() const { return m_logLines.join(QLatin1Char('\n')); }
    int percent() const { return m_percent; }
    QString speed() const { return m_speed; }
    QString status() const { return m_status; }
    QString hostName() const { return m_hostName; }
    QString hostAddress() const { return m_hostAddress; }

    /// Runs a dry-run (--dry-run) to populate the preview change list.
    /// The job map is a QVariantMap built by QML's jobMap() function.
    Q_INVOKABLE void preview(const QVariantMap &job);

    /// Runs a real sync. For delete-enabled jobs, a matching preview fingerprint
    /// is required first (the destructive-run safety gate).
    Q_INVOKABLE void run(const QVariantMap &job);
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
    void hostAddressChanged();
    /// Pushes a job's fields into the QML editor. Emitted on loadJob() (with
    /// the saved job's fields) and newJob() (with blank defaults, archive=on).
    void jobLoaded(const QVariantMap &job);

private:
    void startJob(const SyncJob &job, bool dryRun);
    SyncJob jobFromMap(const QVariantMap &map) const;  ///< QML variant map → SyncJob struct
    QVariantMap mapFromJob(const SyncJob &job) const;  ///< SyncJob struct → QML variant map
    QByteArray syncFingerprint(const SyncJob &job) const; ///< SHA-256 of sync-relevant fields
    void setRunning(bool running);
    void setStatus(const QString &status);
    void appendLog(const QString &line);

    RsyncCapabilities m_caps;
    SyncEngine *m_engine = nullptr;
    ChangeListModel m_changes;
    ProfileStore m_store;
    SecretStore m_secrets;
    JobListModel m_jobs;
    QString m_currentId;
    PeerModel m_peers;
    DiscoveryService *m_discovery = nullptr;
    bool m_discoverable = true;
    Scheduler m_scheduler;

    QStringList m_logLines;
    QString m_status;
    QString m_speed;
    QString m_hostName;
    QString m_hostAddress;
    int m_percent = 0;
    bool m_running = false;
    bool m_activeDryRun = true;
    QByteArray m_activeFingerprint;
    QByteArray m_lastPreviewFingerprint;
    QTimer m_addressTimer;  // re-checks the primary IP for network changes
};
