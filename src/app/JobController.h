#pragma once

#include <QObject>
#include <QString>

#include "engine/BinaryLocator.h"
#include "models/ChangeListModel.h"

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
public:
    explicit JobController(QObject *parent = nullptr);

    ChangeListModel *changes() { return &m_changes; }
    bool running() const { return m_running; }
    bool usingOpenRsync() const { return m_caps.isOpenRsync; }
    QString rsyncSummary() const;
    QString log() const { return m_log; }
    int percent() const { return m_percent; }
    QString status() const { return m_status; }

    // M1: dry-run preview of a local source -> destination.
    Q_INVOKABLE void preview(const QString &source, const QString &destination);
    Q_INVOKABLE void cancel();

signals:
    void runningChanged();
    void logChanged();
    void progressChanged();
    void statusChanged();

private:
    void setRunning(bool running);
    void setStatus(const QString &status);
    void appendLog(const QString &line);

    RsyncCapabilities m_caps;
    RsyncProcessEngine *m_engine = nullptr;
    ChangeListModel m_changes;

    QString m_log;
    QString m_status;
    int m_percent = 0;
    bool m_running = false;
};
