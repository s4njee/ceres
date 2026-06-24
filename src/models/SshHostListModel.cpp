#include "models/SshHostListModel.h"

#include <algorithm>
#include <QHash>

#include "core/Endpoint.h"

namespace {

void addEndpoint(QHash<QString, SshHostListModel::SavedSshHost> &byTarget, const SyncJob &job,
                 const QString &text)
{
    const Endpoint endpoint = EndpointParser::parse(text);
    if (endpoint.kind != EndpointKind::Ssh || endpoint.sshTarget.isEmpty())
        return;

    SshHostListModel::SavedSshHost &saved = byTarget[endpoint.sshTarget];
    if (saved.target.isEmpty()) {
        saved.target = endpoint.sshTarget;
        const int at = endpoint.sshTarget.lastIndexOf(QLatin1Char('@'));
        saved.user = at >= 0 ? endpoint.sshTarget.left(at) : QString();
        saved.host = at >= 0 ? endpoint.sshTarget.mid(at + 1) : endpoint.sshTarget;
        saved.firstJobId = job.id;
        saved.summary = job.name.trimmed().isEmpty() ? QStringLiteral("Untitled sync") : job.name;
    }
    ++saved.jobCount;
}

void addSavedHost(QHash<QString, SshHostListModel::SavedSshHost> &byTarget, const SshHost &host)
{
    if (host.target.isEmpty() || host.host.isEmpty())
        return;

    SshHostListModel::SavedSshHost &saved = byTarget[host.target];
    saved.target = host.target;
    saved.host = host.host;
    saved.user = host.user;
    saved.hasPassword = host.hasPassword;
    if (saved.summary.isEmpty())
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
        if (h.jobCount == 0)
            return h.summary;
        return h.jobCount == 1
            ? h.summary
            : QStringLiteral("%1 saved syncs").arg(h.jobCount);
    case JobCountRole:
        return h.jobCount;
    case FirstJobIdRole:
        return h.firstJobId;
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
        {JobCountRole, "jobCount"},
        {FirstJobIdRole, "firstJobId"},
        {HasPasswordRole, "hasPassword"},
    };
}

void SshHostListModel::setHosts(const QList<SshHost> &savedHosts, const QList<SyncJob> &jobs)
{
    QHash<QString, SavedSshHost> byTarget;
    for (const SshHost &host : savedHosts)
        addSavedHost(byTarget, host);
    for (const SyncJob &job : jobs) {
        addEndpoint(byTarget, job, job.source);
        addEndpoint(byTarget, job, job.destination);
    }

    QList<SavedSshHost> hosts = byTarget.values();
    std::sort(hosts.begin(), hosts.end(), [](const SavedSshHost &a, const SavedSshHost &b) {
        return QString::localeAwareCompare(a.target, b.target) < 0;
    });

    beginResetModel();
    m_hosts = hosts;
    endResetModel();
    emit countChanged();
}
