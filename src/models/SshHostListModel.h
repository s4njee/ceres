#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

#include "core/SshHost.h"
#include "core/SyncJob.h"

/// Backs the sidebar list of saved SSH hosts, derived from saved sync profiles.
///
/// Each row represents one unique SSH target (`user@host` or `host`) found in a
/// saved job's source or destination endpoint. The model keeps a representative
/// job id so the UI can show where the host came from without exposing the full
/// jobs list in the sidebar.
/// @ingroup models
class SshHostListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    struct SavedSshHost {
        QString target;
        QString host;
        QString user;
        QString summary;
        int jobCount = 0;
        QString firstJobId;
        bool hasPassword = false;
    };

    enum Roles {
        TargetRole = Qt::UserRole + 1,
        HostRole,
        UserRole,
        SummaryRole,
        JobCountRole,
        FirstJobIdRole,
        HasPasswordRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setHosts(const QList<SshHost> &savedHosts, const QList<SyncJob> &jobs);

signals:
    void countChanged();

private:
    QList<SavedSshHost> m_hosts;
};
