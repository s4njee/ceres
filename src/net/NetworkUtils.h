#pragma once

#include <QString>
#include <QStringList>

/// @file NetworkUtils.h
/// Utility functions for determining the machine's network identity.
///
/// Used by JobController (to display the host IP in the top bar) and by
/// DiscoveryService (to populate the beacon's address list). Extracted from
/// JobController to keep the controller focused on coordination logic.
///
/// `primaryAddress()` uses a connectionless UDP trick to ask the OS which
/// source IP it would use for the default route — more reliable than guessing
/// by interface name. Falls back to scanning interfaces if offline.

/// @ingroup net
namespace NetworkUtils {

QString detectPrimaryAddress();
QString primaryAddress();
QStringList gatherAddresses();
QString osName();

} // namespace NetworkUtils
