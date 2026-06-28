#pragma once

#include <QString>

/// A short, human-verifiable pairing code for trust-on-first-use device pairing,
/// like a Signal/Syncthing "safety number". Both devices derive the *same* code from
/// their two stable IDs, so a user pairing two Ceres instances can read the code off
/// one screen and confirm it matches the other — proving they're pairing the intended
/// device, not a same-named impostor on the LAN.
///
/// Deterministic and order-independent. Pure and unit-tested.
/// @ingroup core
namespace PairingCode {

/// Six-digit code grouped as "DDD DDD", derived from the two device IDs (the order of
/// the arguments doesn't matter).
QString forDevices(const QString &idA, const QString &idB);

}  // namespace PairingCode
