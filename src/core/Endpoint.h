#pragma once

#include <QString>

#include "core/SyncJob.h"

enum class EndpointKind { Local, Ssh, Daemon };

struct Endpoint {
    QString text;
    EndpointKind kind = EndpointKind::Local;
    QString sshTarget;
    QString sshPath;

    bool isRemote() const { return kind == EndpointKind::Ssh || kind == EndpointKind::Daemon; }
};

class EndpointParser {
public:
    static Endpoint parse(const QString &text);
    static EndpointKind kind(const QString &text) { return parse(text).kind; }
    static QString kindName(const QString &text);
    static bool isDaemon(const QString &text) { return kind(text) == EndpointKind::Daemon; }
    static bool isSsh(const QString &text) { return kind(text) == EndpointKind::Ssh; }
    static bool usesSsh(const SyncJob &job);
    static bool usesDaemon(const SyncJob &job);
};
