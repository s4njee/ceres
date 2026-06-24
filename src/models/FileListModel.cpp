#include "models/FileListModel.h"

#include <algorithm>

int FileListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_entries.size());
}

QVariant FileListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const FileEntry &e = m_entries.at(index.row());
    switch (role) {
    case NameRole:
        return e.name;
    case IsDirRole:
        return e.isDir;
    case IsSymlinkRole:
        return e.isSymlink;
    case SizeRole:
        return e.size;
    case SizeTextRole:
        return e.isDir ? QString() : humanSize(e.size);
    case MtimeTextRole:
        return e.mtimeText;
    default:
        return {};
    }
}

QHash<int, QByteArray> FileListModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {IsDirRole, "isDir"},
        {IsSymlinkRole, "isSymlink"},
        {SizeRole, "size"},
        {SizeTextRole, "sizeText"},
        {MtimeTextRole, "mtimeText"},
    };
}

void FileListModel::setEntries(QList<FileEntry> entries)
{
    std::sort(entries.begin(), entries.end(), [](const FileEntry &a, const FileEntry &b) {
        if (a.isDir != b.isDir)
            return a.isDir;  // directories first
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
    emit countChanged();
}

void FileListModel::clear()
{
    if (m_entries.isEmpty())
        return;
    beginResetModel();
    m_entries.clear();
    endResetModel();
    emit countChanged();
}

bool FileListModel::isDirAt(int row) const
{
    return row >= 0 && row < m_entries.size() && m_entries.at(row).isDir;
}

QString FileListModel::humanSize(qint64 bytes)
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
    // One decimal under 10 (1.5 KB), none above (24 MB) — compact but readable.
    const int decimals = value < 10.0 ? 1 : 0;
    return QString::number(value, 'f', decimals) + QLatin1Char(' ')
        + QLatin1String(units[unit]);
}
