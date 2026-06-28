#pragma once

#include <QString>

/// A Ceres device the user has paired with for mesh sync. Keyed by `deviceId`, the
/// peer's stable machine id (so it's recognized across IP changes and restarts).
/// `sshTarget` is the `user@host` resolved at pairing time — how we reach it to
/// transfer; it's refreshed from discovery when the peer is seen again.
/// @ingroup core
struct PairedDevice {
    QString deviceId;      // stable peer id (Peer::id)
    QString name;          // friendly name, from the peer's advertised name
    QString sshTarget;     // user@host used to sync to it
    qint64 pairedAtMs = 0; // when the pairing was confirmed
};
