#pragma once

#include <QHostAddress>
#include <QList>
#include <QNetworkInterface>
#include <QObject>
#include <QTimer>

#include "core/Peer.h"

class QUdpSocket;
class PeerModel;

/// LAN peer discovery over a UDP multicast beacon.
///
/// Each Ceres instance periodically multicasts a small JSON beacon (see Beacon)
/// to a well-known multicast group and listens for beacons from other instances.
/// Received beacons are fed into a PeerModel, which backs the "On your network"
/// sidebar list. The user can click a discovered peer to pre-fill the destination
/// field with its address.
///
/// Multicast is link-local (same subnet only), so cross-subnet or VPN peers
/// (e.g. Tailscale) are reached via `addManualPeer()` instead.
///
/// Discoverable (advertising) is on by default and can be toggled from the UI.
/// When turned off, the instance still listens for others but stops broadcasting.
/// @ingroup net
class DiscoveryService : public QObject {
    Q_OBJECT
public:
    DiscoveryService(PeerModel *model, Peer self, QObject *parent = nullptr);
    ~DiscoveryService() override;

    void start();
    void setAdvertising(bool on);
    bool advertising() const { return m_advertising; }
    void addManualPeer(const QString &host);

private:
    void sendBeacon(bool leaving);
    void onReadyRead();
    void onExpireTick();

    PeerModel *m_model;
    Peer m_self;
    QUdpSocket *m_socket = nullptr;
    QTimer m_beaconTimer;
    QTimer m_expireTimer;
    QHostAddress m_group;
    quint16 m_port;
    bool m_advertising = true;
    QList<QNetworkInterface> m_sendInterfaces;
};
