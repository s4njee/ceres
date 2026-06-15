#include "net/NetworkUtils.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QSysInfo>
#include <QUdpSocket>

namespace NetworkUtils {

// Best-effort primary LAN IPv4: prefer a physical interface (en0) over virtual
// ones (utun/awdl/Tailscale), which we keep only as a fallback.
QString detectPrimaryAddress()
{
    QString fallback;
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning))
            continue;
        if (flags.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        const QString name = iface.name();
        const bool virtualish = name.startsWith(QLatin1String("utun"))
            || name.startsWith(QLatin1String("awdl")) || name.startsWith(QLatin1String("llw"))
            || name.startsWith(QLatin1String("bridge"));
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol || ip.isLoopback())
                continue;
            if (virtualish) {
                if (fallback.isEmpty())
                    fallback = ip.toString();
                continue;
            }
            return ip.toString();
        }
    }
    return fallback;
}

// The address a peer on the network would actually reach us on: the default-route
// egress IP. A UDP "connect" sends no packets — it just makes the OS pick the
// source IP for the default route, which is more reliable than guessing by
// interface name. Falls back to the interface scan when offline.
QString primaryAddress()
{
    QUdpSocket sock;
    sock.connectToHost(QHostAddress(QStringLiteral("198.51.100.1")), 9);  // TEST-NET-2, no traffic
    const QHostAddress local = sock.localAddress();
    sock.abort();
    if (local.protocol() == QAbstractSocket::IPv4Protocol && !local.isNull() && !local.isLoopback()
        && local.toString() != QLatin1String("0.0.0.0"))
        return local.toString();
    return detectPrimaryAddress();
}

// All non-loopback IPv4 addresses, physical interfaces first, virtual/overlay
// (utun/awdl/Tailscale) after — so the beacon advertises every reachable route.
QStringList gatherAddresses()
{
    QStringList physical;
    QStringList virtualish;
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces) {
        const auto f = iface.flags();
        if (!f.testFlag(QNetworkInterface::IsUp) || !f.testFlag(QNetworkInterface::IsRunning)
            || f.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        const QString name = iface.name();
        const bool v = name.startsWith(QLatin1String("utun")) || name.startsWith(QLatin1String("awdl"))
            || name.startsWith(QLatin1String("llw")) || name.startsWith(QLatin1String("bridge"));
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol || ip.isLoopback())
                continue;
            (v ? virtualish : physical) << ip.toString();
        }
    }
    return physical + virtualish;
}

QString osName()
{
#if defined(Q_OS_MACOS)
    return QStringLiteral("macOS");
#elif defined(Q_OS_WIN)
    return QStringLiteral("Windows");
#else
    return QStringLiteral("Linux");
#endif
}

} // namespace NetworkUtils
