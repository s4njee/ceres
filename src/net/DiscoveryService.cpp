#include "net/DiscoveryService.h"

#include <utility>

#include <QDateTime>
#include <QNetworkDatagram>
#include <QUdpSocket>

#include "models/PeerModel.h"
#include "net/Beacon.h"

namespace {
constexpr int kBeaconIntervalMs = 5000;
constexpr int kExpireIntervalMs = 5000;
constexpr qint64 kPeerTimeoutMs = 15000;  // ~3 missed beacons
}

DiscoveryService::DiscoveryService(PeerModel *model, Peer self, QObject *parent)
    : QObject(parent),
      m_model(model),
      m_self(std::move(self)),
      m_group(QStringLiteral("239.255.42.99")),
      m_port(45888)
{
}

DiscoveryService::~DiscoveryService()
{
    if (m_socket && m_advertising)
        sendBeacon(/*leaving=*/true);  // best-effort "bye" so peers drop us promptly
}

void DiscoveryService::start()
{
    m_socket = new QUdpSocket(this);
    m_socket->bind(QHostAddress::AnyIPv4, m_port,
                   QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces) {
        const auto f = iface.flags();
        if (!f.testFlag(QNetworkInterface::IsUp) || !f.testFlag(QNetworkInterface::IsRunning)
            || f.testFlag(QNetworkInterface::IsLoopBack)
            || !f.testFlag(QNetworkInterface::CanMulticast))
            continue;
        if (m_socket->joinMulticastGroup(m_group, iface))
            m_sendInterfaces << iface;
    }
    if (m_sendInterfaces.isEmpty())
        m_socket->joinMulticastGroup(m_group);  // fall back to the default interface

    connect(m_socket, &QUdpSocket::readyRead, this, &DiscoveryService::onReadyRead);

    m_beaconTimer.setInterval(kBeaconIntervalMs);
    connect(&m_beaconTimer, &QTimer::timeout, this, [this] {
        if (m_advertising)
            sendBeacon(false);
    });
    m_beaconTimer.start();

    m_expireTimer.setInterval(kExpireIntervalMs);
    connect(&m_expireTimer, &QTimer::timeout, this, &DiscoveryService::onExpireTick);
    m_expireTimer.start();

    if (m_advertising)
        sendBeacon(false);  // announce immediately
}

void DiscoveryService::setAdvertising(bool on)
{
    if (m_advertising == on)
        return;
    m_advertising = on;
    if (on)
        sendBeacon(false);
    else
        sendBeacon(true);  // tell peers we've gone quiet
}

void DiscoveryService::addManualPeer(const QString &host)
{
    const QString h = host.trimmed();
    if (h.isEmpty())
        return;
    Peer p;
    p.id = QStringLiteral("manual:") + h;
    p.name = h;
    p.addresses = {h};
    p.os = QStringLiteral("manual");
    p.accepts = {QStringLiteral("ssh")};
    p.manual = true;
    p.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
    m_model->upsert(p);
}

void DiscoveryService::sendBeacon(bool leaving)
{
    if (!m_socket)
        return;
    Peer p = m_self;
    p.leaving = leaving;
    const QByteArray data = Beacon::encode(p);

    if (m_sendInterfaces.isEmpty()) {
        m_socket->writeDatagram(data, m_group, m_port);
        return;
    }
    for (const QNetworkInterface &iface : m_sendInterfaces) {
        m_socket->setMulticastInterface(iface);
        m_socket->writeDatagram(data, m_group, m_port);
    }
}

void DiscoveryService::onReadyRead()
{
    while (m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_socket->receiveDatagram();
        Peer p;
        if (!Beacon::decode(dg.data(), p))
            continue;
        if (p.id == m_self.id)
            continue;  // our own beacon
        if (p.leaving) {
            m_model->removeById(p.id);
            continue;
        }
        p.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
        m_model->upsert(p);
    }
}

void DiscoveryService::onExpireTick()
{
    m_model->removeStale(QDateTime::currentMSecsSinceEpoch(), kPeerTimeoutMs);
}
