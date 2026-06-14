#include "models/JobListModel.h"

int JobListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_jobs.size());
}

QVariant JobListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_jobs.size())
        return {};

    const SyncJob &j = m_jobs.at(index.row());
    switch (role) {
    case IdRole:
        return j.id;
    case NameRole:
        return j.name;
    case SourceRole:
        return j.source;
    case DestinationRole:
        return j.destination;
    case SummaryRole:
        return j.source + QStringLiteral("  →  ") + j.destination;
    default:
        return {};
    }
}

QHash<int, QByteArray> JobListModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {SourceRole, "source"},
        {DestinationRole, "destination"},
        {SummaryRole, "summary"},
    };
}

void JobListModel::setJobs(const QList<SyncJob> &jobs)
{
    beginResetModel();
    m_jobs = jobs;
    endResetModel();
    emit countChanged();
}

int JobListModel::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs.at(i).id == id)
            return i;
    }
    return -1;
}

void JobListModel::upsert(const SyncJob &job)
{
    const int idx = indexOfId(job.id);
    if (idx >= 0) {
        m_jobs[idx] = job;
        const QModelIndex mi = index(idx);
        emit dataChanged(mi, mi);
        return;
    }
    const int row = static_cast<int>(m_jobs.size());
    beginInsertRows(QModelIndex(), row, row);
    m_jobs.append(job);
    endInsertRows();
    emit countChanged();
}

void JobListModel::removeById(const QString &id)
{
    const int idx = indexOfId(id);
    if (idx < 0)
        return;
    beginRemoveRows(QModelIndex(), idx, idx);
    m_jobs.removeAt(idx);
    endRemoveRows();
    emit countChanged();
}

SyncJob JobListModel::jobById(const QString &id) const
{
    const int idx = indexOfId(id);
    return idx >= 0 ? m_jobs.at(idx) : SyncJob{};
}
