#include "core/SecretStore.h"

#include <QProcess>

#if defined(Q_OS_MACOS)
#  include <Security/Security.h>
#endif

namespace {
// Keychain service / libsecret schema name.
const QString kService = QStringLiteral("Ceres daemon password");

#if defined(Q_OS_MACOS)
QByteArray utf8(const QString &s)
{
    return s.toUtf8();
}
#endif
}

bool SecretStore::set(const QString &id, const QString &secret) const
{
    if (id.isEmpty())
        return false;
#if defined(Q_OS_MACOS)
    const QByteArray service = utf8(kService);
    const QByteArray account = utf8(id);
    const QByteArray password = utf8(secret);

    SecKeychainItemRef item = nullptr;
    OSStatus status = SecKeychainFindGenericPassword(
        nullptr, service.size(), service.constData(), account.size(), account.constData(),
        nullptr, nullptr, &item);
    if (status == errSecSuccess && item) {
        status = SecKeychainItemModifyContent(item, nullptr, password.size(), password.constData());
        CFRelease(item);
        return status == errSecSuccess;
    }

    status = SecKeychainAddGenericPassword(
        nullptr, service.size(), service.constData(), account.size(), account.constData(),
        password.size(), password.constData(), nullptr);
    return status == errSecSuccess;
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
    const QByteArray service = utf8(kService);
    const QByteArray account = utf8(id);
    UInt32 length = 0;
    void *data = nullptr;
    const OSStatus status = SecKeychainFindGenericPassword(
        nullptr, service.size(), service.constData(), account.size(), account.constData(),
        &length, &data, nullptr);
    if (status != errSecSuccess || !data)
        return {};
    const QString secret = QString::fromUtf8(static_cast<const char *>(data), length);
    SecKeychainItemFreeContent(nullptr, data);
    return secret;
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
    const QByteArray service = utf8(kService);
    const QByteArray account = utf8(id);
    SecKeychainItemRef item = nullptr;
    const OSStatus findStatus = SecKeychainFindGenericPassword(
        nullptr, service.size(), service.constData(), account.size(), account.constData(),
        nullptr, nullptr, &item);
    if (findStatus == errSecItemNotFound)
        return true;
    if (findStatus != errSecSuccess || !item)
        return false;
    const OSStatus deleteStatus = SecKeychainItemDelete(item);
    CFRelease(item);
    return deleteStatus == errSecSuccess;
#elif defined(Q_OS_LINUX)
    return QProcess::execute(QStringLiteral("secret-tool"),
                             {QStringLiteral("clear"), QStringLiteral("service"), kService,
                              QStringLiteral("account"), id})
        == 0;
#else
    return false;
#endif
}
