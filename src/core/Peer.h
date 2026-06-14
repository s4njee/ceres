#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

// One machine on the network running Ceres, learned from a beacon or added by
// hand. `accepts` lists how you can sync TO it ("ssh", "rsyncd").
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
