#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QSet>

#include "core/Peer.h"

/// Backs the sidebar "On your network" list of discovered and manually-added peers.
///
/// DiscoveryService feeds peers into this model via `upsert()`. Stale peers
/// (no beacon received within the timeout) are pruned by `removeStale()`,
/// which is called on a periodic timer. Manually-added peers are exempt from
/// expiry so they persist across app restarts.
/// @ingroup models
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
        PairedRole,   // the user has paired with this device (PairedDeviceStore)
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void upsert(const Peer &peer);              // add or refresh by id
    void removeById(const QString &id);
    void removeStale(qint64 nowMs, qint64 timeoutMs);  // drops non-manual peers gone quiet

    // The set of paired device ids (drives PairedRole); refreshed by JobController when
    // a pairing changes. Emits a full dataChanged so badges update.
    void setPairedIds(QSet<QString> ids);
    Peer peerById(const QString &id) const;     // empty Peer if not present

signals:
    void countChanged();

private:
    int indexOfId(const QString &id) const;

    QList<Peer> m_peers;
    QSet<QString> m_pairedIds;
};
