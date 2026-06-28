#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include "core/PairedDevice.h"
#include "core/SshHost.h"

/// Serialize/deserialize the user's portable configuration to a single JSON file for
/// backup or moving to another machine: saved SSH hosts, paired mesh devices, and
/// favorite-path bookmarks, plus the external-editor command.
///
/// Deliberately excludes secrets — SSH/daemon passwords live in the OS keychain
/// (SecretStore), never in a store or this bundle — and transient/log data like
/// transfer history. Pure and unit-tested; reading/writing the actual stores happens
/// in the controller.
/// @ingroup core
namespace ConfigBundle {

struct Data {
    QList<SshHost> hosts;
    QList<PairedDevice> devices;
    QMap<QString, QStringList> bookmarks;  // ssh target -> favorite remote paths
    QString editorCommand;
};

/// Pretty-printed JSON bytes for `data` (a schema-tagged object).
QByteArray toJson(const Data &data);

/// Parse a bundle. Returns false if the bytes aren't a recognizable Ceres config
/// bundle; tolerant of individual missing sections.
bool fromJson(const QByteArray &bytes, Data &out);

}  // namespace ConfigBundle
