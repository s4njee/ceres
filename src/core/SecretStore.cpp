#include "core/SecretStore.h"

#include <QProcess>

#if defined(Q_OS_MACOS)
#  include <CoreFoundation/CoreFoundation.h>
#  include <Security/Security.h>
#endif

namespace {
// Keychain service / libsecret schema name.
const QString kService = QStringLiteral("Ceres daemon password");

#if defined(Q_OS_MACOS)
template <typename T>
class ScopedCF {
public:
    explicit ScopedCF(T value = nullptr) : m_value(value) {}
    ~ScopedCF()
    {
        if (m_value)
            CFRelease(m_value);
    }

    ScopedCF(const ScopedCF &) = delete;
    ScopedCF &operator=(const ScopedCF &) = delete;

    T get() const { return m_value; }

private:
    T m_value = nullptr;
};

ScopedCF<CFStringRef> cfString(const QString &s)
{
    return ScopedCF<CFStringRef>(CFStringCreateWithCharacters(
        kCFAllocatorDefault, reinterpret_cast<const UniChar *>(s.utf16()), s.size()));
}

ScopedCF<CFDataRef> cfData(const QByteArray &bytes)
{
    return ScopedCF<CFDataRef>(CFDataCreate(kCFAllocatorDefault,
                                            reinterpret_cast<const UInt8 *>(bytes.constData()),
                                            bytes.size()));
}

CFMutableDictionaryRef passwordQuery(const QString &id)
{
    ScopedCF<CFStringRef> service = cfString(kService);
    ScopedCF<CFStringRef> account = cfString(id);
    if (!service.get() || !account.get())
        return nullptr;

    CFMutableDictionaryRef query = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (!query)
        return nullptr;

    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service.get());
    CFDictionarySetValue(query, kSecAttrAccount, account.get());
    return query;
}
#endif
}

bool SecretStore::set(const QString &id, const QString &secret) const
{
    if (id.isEmpty())
        return false;
#if defined(Q_OS_MACOS)
    ScopedCF<CFMutableDictionaryRef> query(passwordQuery(id));
    ScopedCF<CFDataRef> password = cfData(secret.toUtf8());
    if (!query.get() || !password.get())
        return false;

    CFDictionarySetValue(query.get(), kSecValueData, password.get());
    OSStatus status = SecItemAdd(query.get(), nullptr);
    if (status == errSecDuplicateItem) {
        ScopedCF<CFMutableDictionaryRef> updateQuery(passwordQuery(id));
        ScopedCF<CFMutableDictionaryRef> attributes(CFDictionaryCreateMutable(
            kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        if (!updateQuery.get() || !attributes.get())
            return false;
        CFDictionarySetValue(attributes.get(), kSecValueData, password.get());
        status = SecItemUpdate(updateQuery.get(), attributes.get());
    }
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
    ScopedCF<CFMutableDictionaryRef> query(passwordQuery(id));
    if (!query.get())
        return {};
    CFDictionarySetValue(query.get(), kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query.get(), kSecMatchLimit, kSecMatchLimitOne);

    CFTypeRef result = nullptr;
    const OSStatus status = SecItemCopyMatching(query.get(), &result);
    if (status != errSecSuccess || !result)
        return {};
    ScopedCF<CFTypeRef> data(result);
    if (CFGetTypeID(data.get()) != CFDataGetTypeID())
        return {};
    const auto *bytes = reinterpret_cast<const char *>(CFDataGetBytePtr(static_cast<CFDataRef>(data.get())));
    const int length = static_cast<int>(CFDataGetLength(static_cast<CFDataRef>(data.get())));
    return QString::fromUtf8(bytes, length);
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
    ScopedCF<CFMutableDictionaryRef> query(passwordQuery(id));
    if (!query.get())
        return false;
    const OSStatus status = SecItemDelete(query.get());
    return status == errSecSuccess || status == errSecItemNotFound;
#elif defined(Q_OS_LINUX)
    return QProcess::execute(QStringLiteral("secret-tool"),
                             {QStringLiteral("clear"), QStringLiteral("service"), kService,
                              QStringLiteral("account"), id})
        == 0;
#else
    return false;
#endif
}
