#include "models/PeerModel.h"

int PeerModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_peers.size());
}

QVariant PeerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_peers.size())
        return {};

    const Peer &p = m_peers.at(index.row());
    switch (role) {
    case IdRole:
        return p.id;
    case NameRole:
        return p.name;
    case AddressRole:
        return p.primaryAddress();
    case OsRole:
        return p.os;
    case AcceptsRole:
        return p.isDaemon() ? QStringLiteral("rsync daemon") : QStringLiteral("over SSH");
    case DaemonRole:
        return p.isDaemon();
    case ManualRole:
        return p.manual;
    case PairedRole:
        return m_pairedIds.contains(p.id);
    default:
        return {};
    }
}

QHash<int, QByteArray> PeerModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {AddressRole, "address"},
        {OsRole, "os"},
        {AcceptsRole, "accepts"},
        {DaemonRole, "daemon"},
        {ManualRole, "manual"},
        {PairedRole, "paired"},
    };
}

void PeerModel::setPairedIds(QSet<QString> ids)
{
    if (ids == m_pairedIds)
        return;
    m_pairedIds = std::move(ids);
    if (!m_peers.isEmpty())
        emit dataChanged(index(0), index(static_cast<int>(m_peers.size()) - 1), {PairedRole});
}

Peer PeerModel::peerById(const QString &id) const
{
    const int idx = indexOfId(id);
    return idx >= 0 ? m_peers.at(idx) : Peer{};
}

int PeerModel::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_peers.size(); ++i) {
        if (m_peers.at(i).id == id)
            return i;
    }
    return -1;
}

void PeerModel::upsert(const Peer &peer)
{
    const int idx = indexOfId(peer.id);
    if (idx >= 0) {
        m_peers[idx] = peer;
        const QModelIndex mi = index(idx);
        emit dataChanged(mi, mi);
        return;
    }
    const int row = static_cast<int>(m_peers.size());
    beginInsertRows(QModelIndex(), row, row);
    m_peers.append(peer);
    endInsertRows();
    emit countChanged();
}

void PeerModel::removeById(const QString &id)
{
    const int idx = indexOfId(id);
    if (idx < 0)
        return;
    beginRemoveRows(QModelIndex(), idx, idx);
    m_peers.removeAt(idx);
    endRemoveRows();
    emit countChanged();
}

void PeerModel::removeStale(qint64 nowMs, qint64 timeoutMs)
{
    for (int i = m_peers.size() - 1; i >= 0; --i) {
        const Peer &p = m_peers.at(i);
        if (p.manual)
            continue;
        if (nowMs - p.lastSeenMs > timeoutMs) {
            beginRemoveRows(QModelIndex(), i, i);
            m_peers.removeAt(i);
            endRemoveRows();
            emit countChanged();
        }
    }
}
