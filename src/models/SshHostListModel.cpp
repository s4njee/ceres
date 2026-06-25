#include "models/SshHostListModel.h"

#include <algorithm>
#include <QHash>

namespace {

void addSavedHost(QHash<QString, SshHostListModel::SavedSshHost> &byTarget, const SshHost &host)
{
    if (host.target.isEmpty() || host.host.isEmpty())
        return;

    SshHostListModel::SavedSshHost &saved = byTarget[host.target];
    saved.target = host.target;
    saved.host = host.host;
    saved.user = host.user;
    saved.hasPassword = host.hasPassword;
    saved.summary = host.hasPassword ? QStringLiteral("saved credentials")
                                     : QStringLiteral("saved host");
}

} // namespace

int SshHostListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_hosts.size());
}

QVariant SshHostListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_hosts.size())
        return {};

    const SavedSshHost &h = m_hosts.at(index.row());
    switch (role) {
    case TargetRole:
        return h.target;
    case HostRole:
        return h.host;
    case UserRole:
        return h.user;
    case SummaryRole:
        return h.summary;
    case HasPasswordRole:
        return h.hasPassword;
    default:
        return {};
    }
}

QHash<int, QByteArray> SshHostListModel::roleNames() const
{
    return {
        {TargetRole, "target"},
        {HostRole, "host"},
        {UserRole, "user"},
        {SummaryRole, "summary"},
        {HasPasswordRole, "hasPassword"},
    };
}

void SshHostListModel::setHosts(const QList<SshHost> &savedHosts)
{
    QHash<QString, SavedSshHost> byTarget;
    for (const SshHost &host : savedHosts)
        addSavedHost(byTarget, host);

    QList<SavedSshHost> hosts = byTarget.values();
    std::sort(hosts.begin(), hosts.end(), [](const SavedSshHost &a, const SavedSshHost &b) {
        return QString::localeAwareCompare(a.target, b.target) < 0;
    });

    beginResetModel();
    m_hosts = hosts;
    endResetModel();
    emit countChanged();
}
