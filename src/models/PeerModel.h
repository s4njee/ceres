#pragma once

#include <QAbstractListModel>
#include <QList>

#include "core/Peer.h"

// Backs the sidebar "On your network" list of discovered/manual peers.
class PeerModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        AddressRole,
        OsRole,
        AcceptsRole,  // human-readable: "over SSH" / "rsync daemon"
        DaemonRole,
        ManualRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void upsert(const Peer &peer);              // add or refresh by id
    void removeById(const QString &id);
    void removeStale(qint64 nowMs, qint64 timeoutMs);  // drops non-manual peers gone quiet

signals:
    void countChanged();

private:
    int indexOfId(const QString &id) const;

    QList<Peer> m_peers;
};
