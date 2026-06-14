#pragma once

#include <QStringList>

#include "core/SyncJob.h"
#include "engine/BinaryLocator.h"  // RsyncCapabilities

// Translates a SyncJob into an rsync argument vector. Pure and capability-aware:
// progress2 / outbuf / no-inc-recursive are only emitted when the located rsync
// actually supports them (macOS' system openrsync does not). Kept free of side
// effects so it is exhaustively unit-testable.
class ArgvBuilder {
public:
    static QStringList build(const SyncJob &job, const RsyncCapabilities &caps, bool dryRun);
};
