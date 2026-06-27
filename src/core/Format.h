#pragma once

#include <QLatin1Char>
#include <QLatin1String>
#include <QString>

/// Small presentation-formatting helpers shared across models, controllers, and the
/// transfer engine so there's exactly one definition of each. Header-only and
/// dependency-free (Qt Core only).
/// @ingroup core
namespace Format {

/// Human-readable byte count, e.g. 1536 -> "1.5 KB", 24*1024 -> "24 KB". One decimal
/// below 10 (compact), none at/above 10. Negative input yields an empty string.
inline QString humanSize(qint64 bytes)
{
    if (bytes < 0)
        return {};
    if (bytes < 1024)
        return QString::number(bytes) + QStringLiteral(" B");

    static const char *units[] = {"KB", "MB", "GB", "TB", "PB"};
    double value = static_cast<double>(bytes) / 1024.0;
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    const int decimals = value < 10.0 ? 1 : 0;
    return QString::number(value, 'f', decimals) + QLatin1Char(' ') + QLatin1String(units[unit]);
}

}  // namespace Format
