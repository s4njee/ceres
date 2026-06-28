#include "core/ConfigBundle.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/PairedDeviceStore.h"
#include "core/SshHostStore.h"

namespace {
constexpr int kSchema = 1;
}

namespace ConfigBundle {

QByteArray toJson(const Data &data)
{
    QJsonObject root;
    root[QStringLiteral("schema")] = kSchema;
    root[QStringLiteral("app")] = QStringLiteral("Ceres");

    QJsonArray hosts;
    for (const SshHost &h : data.hosts)
        hosts.append(SshHostStore::toJson(h));
    root[QStringLiteral("hosts")] = hosts;

    QJsonArray devices;
    for (const PairedDevice &d : data.devices)
        devices.append(PairedDeviceStore::toJson(d));
    root[QStringLiteral("pairedDevices")] = devices;

    QJsonObject bookmarks;
    for (auto it = data.bookmarks.cbegin(); it != data.bookmarks.cend(); ++it) {
        QJsonArray paths;
        for (const QString &p : it.value())
            paths.append(p);
        bookmarks[it.key()] = paths;
    }
    root[QStringLiteral("bookmarks")] = bookmarks;

    if (!data.editorCommand.isEmpty())
        root[QStringLiteral("editorCommand")] = data.editorCommand;

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool fromJson(const QByteArray &bytes, Data &out)
{
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject())
        return false;
    const QJsonObject root = doc.object();
    // Accept any object that looks like ours (has at least one known section).
    if (root.value(QStringLiteral("app")).toString() != QLatin1String("Ceres")
        && !root.contains(QStringLiteral("hosts")))
        return false;

    out = Data{};

    for (const QJsonValue &v : root.value(QStringLiteral("hosts")).toArray()) {
        if (!v.isObject())
            continue;
        const SshHost h = SshHostStore::fromJson(v.toObject());
        if (!h.target.isEmpty())
            out.hosts << h;
    }

    for (const QJsonValue &v : root.value(QStringLiteral("pairedDevices")).toArray()) {
        if (!v.isObject())
            continue;
        const PairedDevice d = PairedDeviceStore::fromJson(v.toObject());
        if (!d.deviceId.isEmpty())
            out.devices << d;
    }

    const QJsonObject bookmarks = root.value(QStringLiteral("bookmarks")).toObject();
    for (auto it = bookmarks.begin(); it != bookmarks.end(); ++it) {
        QStringList paths;
        for (const QJsonValue &p : it.value().toArray())
            paths << p.toString();
        if (!paths.isEmpty())
            out.bookmarks.insert(it.key(), paths);
    }

    out.editorCommand = root.value(QStringLiteral("editorCommand")).toString();
    return true;
}

}  // namespace ConfigBundle
