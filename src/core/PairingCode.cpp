#include "core/PairingCode.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QStringList>

namespace PairingCode {

QString forDevices(const QString &idA, const QString &idB)
{
    // Order-independent: sort the two IDs so both ends hash the same input.
    QStringList ids{idA.trimmed(), idB.trimmed()};
    ids.sort();

    const QByteArray digest = QCryptographicHash::hash(
        (ids.at(0) + QLatin1Char('\n') + ids.at(1)).toUtf8(), QCryptographicHash::Sha256);

    // First 4 bytes -> a 6-digit number.
    quint32 n = (static_cast<quint8>(digest[0]) << 24) | (static_cast<quint8>(digest[1]) << 16)
              | (static_cast<quint8>(digest[2]) << 8) | static_cast<quint8>(digest[3]);
    const QString six = QStringLiteral("%1").arg(n % 1000000u, 6, 10, QLatin1Char('0'));
    return six.left(3) + QLatin1Char(' ') + six.mid(3);
}

}  // namespace PairingCode
