#include "core/SecretStore.h"

#include <QProcess>

namespace {
// Keychain service / libsecret schema name.
const QString kService = QStringLiteral("Ceres daemon password");
}

bool SecretStore::set(const QString &id, const QString &secret) const
{
    if (id.isEmpty())
        return false;
#if defined(Q_OS_MACOS)
    // -U: update if present. -A: no per-app ACL, so ceres-runner can read what
    // the GUI wrote without a prompt. (Password is briefly visible in `ps` via
    // -w; acceptable for v1, tightened by signing + an access group in M6.)
    return QProcess::execute(
               QStringLiteral("/usr/bin/security"),
               {QStringLiteral("add-generic-password"), QStringLiteral("-U"), QStringLiteral("-A"),
                QStringLiteral("-s"), kService, QStringLiteral("-a"), id, QStringLiteral("-w"), secret})
        == 0;
#elif defined(Q_OS_LINUX)
    QProcess p;
    p.start(QStringLiteral("secret-tool"),
            {QStringLiteral("store"), QStringLiteral("--label=Ceres daemon"),
             QStringLiteral("service"), kService, QStringLiteral("account"), id});
    if (!p.waitForStarted(2000))
        return false;
    p.write(secret.toUtf8());  // secret-tool reads the secret from stdin (not argv)
    p.closeWriteChannel();
    return p.waitForFinished(3000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
#else
    Q_UNUSED(secret);
    return false;
#endif
}

QString SecretStore::get(const QString &id) const
{
    if (id.isEmpty())
        return {};
    // Strip only the single trailing newline the tool appends — never use
    // trimmed(), which would corrupt a password with leading/trailing whitespace.
    const auto chompOne = [](QByteArray out) {
        if (out.endsWith('\n'))
            out.chop(1);
        if (out.endsWith('\r'))
            out.chop(1);
        return QString::fromUtf8(out);
    };
#if defined(Q_OS_MACOS)
    QProcess p;
    p.start(QStringLiteral("/usr/bin/security"),
            {QStringLiteral("find-generic-password"), QStringLiteral("-s"), kService,
             QStringLiteral("-a"), id, QStringLiteral("-w")});
    if (!p.waitForStarted(2000))
        return {};
    p.waitForFinished(3000);
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return {};
    return chompOne(p.readAllStandardOutput());
#elif defined(Q_OS_LINUX)
    QProcess p;
    p.start(QStringLiteral("secret-tool"),
            {QStringLiteral("lookup"), QStringLiteral("service"), kService, QStringLiteral("account"), id});
    if (!p.waitForStarted(2000))
        return {};
    p.waitForFinished(3000);
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return {};
    return chompOne(p.readAllStandardOutput());
#else
    Q_UNUSED(chompOne);
    return {};
#endif
}

bool SecretStore::remove(const QString &id) const
{
    if (id.isEmpty())
        return false;
#if defined(Q_OS_MACOS)
    return QProcess::execute(QStringLiteral("/usr/bin/security"),
                             {QStringLiteral("delete-generic-password"), QStringLiteral("-s"), kService,
                              QStringLiteral("-a"), id})
        == 0;
#elif defined(Q_OS_LINUX)
    return QProcess::execute(QStringLiteral("secret-tool"),
                             {QStringLiteral("clear"), QStringLiteral("service"), kService,
                              QStringLiteral("account"), id})
        == 0;
#else
    return false;
#endif
}
