#pragma once

#include <QString>

#include "core/SyncJob.h"

/// The three kinds of rsync target. The parser distinguishes them by syntax:
///   Local  — plain filesystem path (e.g. "/Users/me/docs")
///   Ssh    — `user@host:/path` or `host:/path` (goes over SSH transport)
///   Daemon — rsync://host/module or host::module (native rsync protocol)
/// This classification drives UI behaviour (showing SSH options vs daemon
/// password), argv construction, and remote tab-completion routing.
enum class EndpointKind { Local, Ssh, Daemon };

/// Parsed representation of a source or destination string entered by the user.
/// For SSH endpoints, `sshTarget` and `sshPath` are split out so the UI and
/// ArgvBuilder can work with each piece independently.
/// @ingroup core
struct Endpoint {
    QString text;
    EndpointKind kind = EndpointKind::Local;
    QString sshTarget;
    QString sshPath;

    bool isRemote() const { return kind == EndpointKind::Ssh || kind == EndpointKind::Daemon; }
};

/// @ingroup core
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
