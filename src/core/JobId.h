#pragma once

#include <QString>

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
