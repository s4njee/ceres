#include "core/SshConfigImport.h"

#include <QDir>
#include <QRegularExpression>
#include <QStringList>

namespace {
// Expand a leading "~" / "~/" in an IdentityFile path to the user's home directory.
QString expandTilde(const QString &path)
{
    if (path == QLatin1String("~"))
        return QDir::homePath();
    if (path.startsWith(QLatin1String("~/")))
        return QDir::homePath() + path.mid(1);
    return path;
}

// Strip matching surrounding quotes from a config value.
QString unquote(QString v)
{
    if (v.size() >= 2 && ((v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"')))
                          || (v.startsWith(QLatin1Char('\'')) && v.endsWith(QLatin1Char('\'')))))
        return v.mid(1, v.size() - 2);
    return v;
}
}  // namespace

QList<SshHost> SshConfigImport::parse(const QString &configText)
{
    QList<SshHost> out;

    QStringList aliases;
    QString hostName, user, identity;
    int port = 0;

    auto flush = [&] {
        for (const QString &alias : aliases) {
            // Skip wildcard/negated patterns — they're match rules, not concrete hosts.
            if (alias.contains(QLatin1Char('*')) || alias.contains(QLatin1Char('?'))
                || alias.startsWith(QLatin1Char('!')))
                continue;
            const QString hn = hostName.isEmpty() ? alias : hostName;
            SshHost h;
            h.host = hn;
            h.user = user;
            h.target = user.isEmpty() ? hn : (user + QLatin1Char('@') + hn);
            h.label = alias;
            h.sshKeyPath = identity;
            h.sshPort = port;
            out << h;
        }
        aliases.clear();
        hostName.clear();
        user.clear();
        identity.clear();
        port = 0;
    };

    const QStringList lines = configText.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        // A keyword and its value are separated by whitespace and/or a single '='.
        int i = 0;
        while (i < line.size() && !line.at(i).isSpace() && line.at(i) != QLatin1Char('='))
            ++i;
        const QString key = line.left(i).toLower();
        while (i < line.size() && (line.at(i).isSpace() || line.at(i) == QLatin1Char('=')))
            ++i;
        const QString value = unquote(line.mid(i).trimmed());
        if (key.isEmpty() || value.isEmpty())
            continue;

        if (key == QLatin1String("host")) {
            flush();  // close the previous block before starting a new one
            aliases = value.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        } else if (key == QLatin1String("hostname")) {
            hostName = value;
        } else if (key == QLatin1String("user")) {
            user = value;
        } else if (key == QLatin1String("port")) {
            port = qBound(0, value.toInt(), 65535);
        } else if (key == QLatin1String("identityfile") && identity.isEmpty()) {
            identity = expandTilde(value);  // keep the first IdentityFile only
        }
    }
    flush();
    return out;
}
