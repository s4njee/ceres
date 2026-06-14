#pragma once

#include <QByteArray>

#include "core/Peer.h"

// Wire format for discovery beacons (compact JSON). Pure and unit-tested.
class Beacon {
public:
    static QByteArray encode(const Peer &peer);
    // Returns false if the payload isn't a valid Ceres beacon.
    static bool decode(const QByteArray &data, Peer &out);
};
