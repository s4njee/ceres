#pragma once

#include <QList>
#include <QString>

#include "core/SshHost.h"

/// Parses an OpenSSH client config (`~/.ssh/config`) into saved-host profiles.
///
/// Pulls each concrete `Host` alias (wildcard patterns like `*` / `?` are skipped)
/// together with its `HostName`, `User`, `Port`, and first `IdentityFile`, mapping
/// them onto SshHost: the alias becomes the label, the real hostname (or the alias
/// if no HostName) the connection target. Pure and stateless so it's unit-testable
/// without touching the filesystem. Lives in ceres_core (no GUI dependency).
/// @ingroup core
namespace SshConfigImport {

/// Parse the text of an ssh config into host profiles (order preserved).
QList<SshHost> parse(const QString &configText);

}  // namespace SshConfigImport
