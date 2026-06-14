#pragma once

#include <QObject>
#include <QString>

#include "core/ProfileStore.h"
#include "engine/BinaryLocator.h"
#include "models/ChangeListModel.h"
#include "models/JobListModel.h"

class RsyncProcessEngine;

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
public:
    explicit JobController(QObject *parent = nullptr);

    ChangeListModel *changes() { return &m_changes; }
    JobListModel *jobs() { return &m_jobs; }
    QString currentId() const { return m_currentId; }
    bool running() const { return m_running; }
    bool usingOpenRsync() const { return m_caps.isOpenRsync; }
    QString rsyncSummary() const;
    QString log() const { return m_log; }
    int percent() const { return m_percent; }
    QString status() const { return m_status; }
    QString hostName() const { return m_hostName; }
    QString hostAddress() const { return m_hostAddress; }

    // Dry-run preview of source -> destination with the chosen flags.
    Q_INVOKABLE void preview(const QString &source, const QString &destination,
                             bool archive, bool compress, bool deleteExtras, bool checksum);
    // Real (non-dry-run) sync. Callers gate destructive deletes in the UI first.
    Q_INVOKABLE void run(const QString &source, const QString &destination,
                         bool archive, bool compress, bool deleteExtras, bool checksum);
    Q_INVOKABLE void cancel();

    // Profile management (sidebar jobs).
    Q_INVOKABLE void newJob();              // reset the editor to a blank, unsaved job
    Q_INVOKABLE void loadJob(const QString &id);
    Q_INVOKABLE void saveJob(const QString &name, const QString &source, const QString &destination,
                             bool archive, bool compress, bool deleteExtras, bool checksum);
    Q_INVOKABLE void deleteJob(const QString &id);

signals:
    void runningChanged();
    void logChanged();
    void progressChanged();
    void statusChanged();
    void currentChanged();
    // Pushes a job's fields into the editor (new job -> all blank, archive on).
    void jobLoaded(const QString &name, const QString &source, const QString &destination,
                   bool archive, bool compress, bool deleteExtras, bool checksum);

private:
    void startJob(const QString &source, const QString &destination, bool archive,
                  bool compress, bool deleteExtras, bool checksum, bool dryRun);
    void setRunning(bool running);
    void setStatus(const QString &status);
    void appendLog(const QString &line);

    RsyncCapabilities m_caps;
    RsyncProcessEngine *m_engine = nullptr;
    ChangeListModel m_changes;
    ProfileStore m_store;
    JobListModel m_jobs;
    QString m_currentId;

    QString m_log;
    QString m_status;
    QString m_hostName;
    QString m_hostAddress;
    int m_percent = 0;
    bool m_running = false;
    bool m_activeDryRun = true;
};
