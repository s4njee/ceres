#pragma once

#include <QString>
#include <QStringList>

#include "core/SyncJob.h"
#include "engine/BinaryLocator.h"
#include "engine/RsyncEvents.h"

namespace AdHocTransfer {

SyncJob makeJob(const QString &source, const QString &destination);
QStringList buildArguments(const QString &source, const QString &destination,
                           const RsyncCapabilities &caps);
QString renderProgressLine(const ProgressInfo &progress, int columns, bool color);
int run(const QString &source, const QString &destination);

} // namespace AdHocTransfer
