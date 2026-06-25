#include "app/Notifier.h"

#include <QProcess>

namespace {
// AppleScript string literals escape backslash and double-quote with a backslash.
QString osaQuote(QString s)
{
    s.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    s.replace(QLatin1Char('"'), QLatin1String("\\\""));
    return s;
}
}  // namespace

void Notifier::notify(const QString &title, const QString &body)
{
#if defined(Q_OS_MACOS)
    const QString script = QStringLiteral("display notification \"%1\" with title \"%2\"")
                               .arg(osaQuote(body), osaQuote(title));
    QProcess::startDetached(QStringLiteral("osascript"), {QStringLiteral("-e"), script});
#elif defined(Q_OS_WIN)
    Q_UNUSED(title);
    Q_UNUSED(body);  // no dependency-free toast API; left as a no-op for now
#else
    // freedesktop notification daemon; -a sets the app name shown by the daemon.
    QProcess::startDetached(QStringLiteral("notify-send"),
                            {QStringLiteral("-a"), QStringLiteral("Ceres"), title, body});
#endif
}
