#pragma once

#include <QString>

/// Validates that a job ID is safe to use as a filename and launchd/systemd unit
/// identifier. IDs are stored as `<id>.json` profile files and embedded into OS
/// scheduler unit names (e.g. `com.ceres.job.<id>`), so they must be restricted
/// to a safe character set to prevent path-traversal or injection attacks.
/// Allows: ASCII letters, digits, hyphens, underscores, and dots (no `.` or `..`).
inline bool isSafeJobId(const QString &id)
{
    if (id.isEmpty() || id.size() > 128)
        return false;

    for (const QChar ch : id) {
        const ushort c = ch.unicode();
        const bool asciiLetter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        const bool asciiDigit = c >= '0' && c <= '9';
        if (!asciiLetter && !asciiDigit && ch != QLatin1Char('-') && ch != QLatin1Char('_')
            && ch != QLatin1Char('.'))
            return false;
    }

    return id != QLatin1String(".") && id != QLatin1String("..");
}
