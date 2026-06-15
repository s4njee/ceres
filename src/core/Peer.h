#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

/// One machine on the network running Ceres, learned either automatically
/// via UDP multicast beacons (DiscoveryService) or added manually by the user
/// typing a host/IP in the sidebar. `accepts` lists how you can sync TO it
/// ("ssh", "rsyncd"), which determines whether the UI pre-fills an SSH or
/// daemon-style destination when the peer is clicked.
///
/// Peers have a TTL: beacon-discovered peers expire after 30s of silence,
/// while manually-added peers (`manual = true`) persist across restarts.
/// @ingroup core
struct Peer {
    QString id;            // stable machine id (or "manual:<host>")
    QString name;
    QStringList addresses; // primary first
    QString os;
    QString version;
    QStringList accepts;
    bool manual = false;   // added by host, never expired
    bool leaving = false;  // a "bye" beacon
    qint64 lastSeenMs = 0;

    QString primaryAddress() const { return addresses.isEmpty() ? QString() : addresses.first(); }
    bool isDaemon() const
    {
        return accepts.contains(QStringLiteral("rsyncd")) && !accepts.contains(QStringLiteral("ssh"));
    }
};
