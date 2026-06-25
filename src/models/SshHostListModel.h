#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

#include "core/SshHost.h"

/// Backs the sidebar list of saved SSH hosts.
///
/// Each row represents one saved SSH target (`user@host` or `host`) added via the
/// New host dialog (or remembered from a password prompt) and persisted by
/// SshHostStore.
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
        bool hasPassword = false;
    };

    enum Roles {
        TargetRole = Qt::UserRole + 1,
        HostRole,
        UserRole,
        SummaryRole,
        HasPasswordRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setHosts(const QList<SshHost> &savedHosts);

signals:
    void countChanged();

private:
    QList<SavedSshHost> m_hosts;
};
