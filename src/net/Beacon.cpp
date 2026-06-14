#include "net/Beacon.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
constexpr int kProtocol = 1;
}

QByteArray Beacon::encode(const Peer &p)
{
    QJsonObject o;
    o[QStringLiteral("ceres")] = kProtocol;
    o[QStringLiteral("id")] = p.id;
    o[QStringLiteral("name")] = p.name;
    o[QStringLiteral("addrs")] = QJsonArray::fromStringList(p.addresses);
    o[QStringLiteral("os")] = p.os;
    o[QStringLiteral("version")] = p.version;
    o[QStringLiteral("accepts")] = QJsonArray::fromStringList(p.accepts);
    if (p.leaving)
        o[QStringLiteral("bye")] = true;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

bool Beacon::decode(const QByteArray &data, Peer &out)
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return false;
    const QJsonObject o = doc.object();
    if (o.value(QStringLiteral("ceres")).toInt() != kProtocol)
        return false;

    out.id = o.value(QStringLiteral("id")).toString();
    if (out.id.isEmpty())
        return false;

    out.name = o.value(QStringLiteral("name")).toString();
    out.addresses.clear();
    for (const QJsonValue &v : o.value(QStringLiteral("addrs")).toArray())
        out.addresses << v.toString();
    out.os = o.value(QStringLiteral("os")).toString();
    out.version = o.value(QStringLiteral("version")).toString();
    out.accepts.clear();
    for (const QJsonValue &v : o.value(QStringLiteral("accepts")).toArray())
        out.accepts << v.toString();
    out.leaving = o.value(QStringLiteral("bye")).toBool(false);
    return true;
}
