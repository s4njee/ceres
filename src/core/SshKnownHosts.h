#pragma once

#include <QString>

#include "engine/BinaryLocator.h"

struct KnownHostRepairResult {
    bool ok = false;
    QString message;
};

class SshKnownHosts {
public:
    static bool looksLikeChangedHostKey(const QString &stderrText);
    static QString hostFromTarget(const QString &target);
    static KnownHostRepairResult removeHost(const RsyncCapabilities &caps, const QString &target,
                                            int port);

private:
    static QString knownHostsPath();
    static QString sshKeygenProgram(const RsyncCapabilities &caps);
    static QString knownHostsPattern(const QString &target, int port);
};
