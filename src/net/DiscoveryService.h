#pragma once

#include <QHostAddress>
#include <QList>
#include <QNetworkInterface>
#include <QObject>
#include <QTimer>

#include "core/Peer.h"

class QUdpSocket;
class PeerModel;

// LAN peer discovery over a UDP multicast beacon. Each instance periodically
// multicasts a small JSON beacon and listens for others, feeding a PeerModel.
// Multicast is link-local, so cross-subnet / Tailscale peers are reached via
// addManualPeer() instead. Discoverable (advertising) is on by default.
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
