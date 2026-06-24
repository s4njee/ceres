#include "models/TransfersModel.h"

int TransfersModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant TransfersModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const Row &r = m_rows.at(index.row());
    switch (role) {
    case IdRole:
        return r.id;
    case NameRole:
        return r.name;
    case DirectionRole:
        return r.direction;
    case SourceRole:
        return r.source;
    case DestinationRole:
        return r.destination;
    case StatusRole:
        return static_cast<int>(r.status);
    case StatusTextRole:
        switch (r.status) {
        case Queued:
            return QStringLiteral("Queued");
        case Active:
            return QStringLiteral("Transferring…");
        case Done:
            return QStringLiteral("Done");
        case Failed:
            return QStringLiteral("Failed");
        case Cancelled:
            return QStringLiteral("Cancelled");
        }
        return {};
    case PercentRole:
        return r.percent;
    case SpeedRole:
        return r.speed;
    case ErrorRole:
        return r.error;
    default:
        return {};
    }
}

QHash<int, QByteArray> TransfersModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {DirectionRole, "direction"},
        {SourceRole, "source"},
        {DestinationRole, "destination"},
        {StatusRole, "status"},
        {StatusTextRole, "statusText"},
        {PercentRole, "percent"},
        {SpeedRole, "speed"},
        {ErrorRole, "error"},
    };
}

int TransfersModel::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).id == id)
            return i;
    }
    return -1;
}

void TransfersModel::add(const QString &id, const QString &name, const QString &direction,
                         const QString &source, const QString &destination)
{
    const int row = static_cast<int>(m_rows.size());
    beginInsertRows(QModelIndex(), row, row);
    Row r;
    r.id = id;
    r.name = name;
    r.direction = direction;
    r.source = source;
    r.destination = destination;
    r.status = Queued;
    m_rows.append(r);
    endInsertRows();
    emit countChanged();
}

void TransfersModel::setStatus(const QString &id, Status status, const QString &error)
{
    const int idx = indexOfId(id);
    if (idx < 0)
        return;

    Row &r = m_rows[idx];
    if (r.status == status && r.error == error)
        return;

    r.status = status;
    r.error = error;
    const QModelIndex mi = index(idx);
    emit dataChanged(mi, mi);
    // Any status transition can move a row into or out of Active, so refresh the
    // tally; activeCount() recomputes from scratch so over-signalling is harmless.
    emit activeCountChanged();
}

void TransfersModel::updateProgress(const QString &id, int percent, const QString &speed)
{
    const int idx = indexOfId(id);
    if (idx < 0)
        return;

    Row &r = m_rows[idx];
    r.percent = percent;
    r.speed = speed;
    const QModelIndex mi = index(idx);
    emit dataChanged(mi, mi);
}

void TransfersModel::clearCompleted()
{
    // Walk from the end so removed indices don't shift the rows still to inspect.
    for (int i = static_cast<int>(m_rows.size()) - 1; i >= 0; --i) {
        const Status s = m_rows.at(i).status;
        if (s == Done || s == Failed || s == Cancelled) {
            beginRemoveRows(QModelIndex(), i, i);
            m_rows.removeAt(i);
            endRemoveRows();
        }
    }
    emit countChanged();
}

int TransfersModel::activeCount() const
{
    int n = 0;
    for (const Row &r : m_rows) {
        if (r.status == Active)
            ++n;
    }
    return n;
}
