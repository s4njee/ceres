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

namespace {
// File-type key: the lowercase extension, so same-type files group together.
QString extensionOf(const QString &name)
{
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    return dot > 0 ? name.mid(dot + 1).toLower() : QString();
}
} // namespace

void FileListModel::sortSource()
{
    const int key = m_sortKey;
    const bool asc = m_sortAscending;
    std::sort(m_source.begin(), m_source.end(), [key, asc](const FileEntry &a, const FileEntry &b) {
        if (a.isDir != b.isDir)
            return a.isDir;  // directories always group first, independent of key/direction

        int cmp = 0;
        switch (key) {
        case BySize:
            cmp = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0);
            break;
        case ByDate:
            cmp = a.mtime < b.mtime ? -1 : (a.mtime > b.mtime ? 1 : 0);
            break;
        case ByType:
            cmp = extensionOf(a.name).compare(extensionOf(b.name));
            break;
        case ByName:
        default:
            break;
        }
        if (cmp == 0)  // stable tiebreak (and the whole comparison for ByName)
            cmp = a.name.compare(b.name, Qt::CaseInsensitive);
        return asc ? cmp < 0 : cmp > 0;
    });
}

void FileListModel::rebuildView()
{
    // Caller wraps this in begin/endResetModel. Project the sorted source through the
    // case-insensitive name filter (empty filter = everything).
    if (m_filter.isEmpty()) {
        m_entries = m_source;
        return;
    }
    m_entries.clear();
    for (const FileEntry &e : m_source) {
        if (e.name.contains(m_filter, Qt::CaseInsensitive))
            m_entries.append(e);
    }
}

void FileListModel::setSort(int key, bool ascending)
{
    if (key < ByName || key > ByType)
        return;
    if (m_sortKey == key && m_sortAscending == ascending)
        return;
    m_sortKey = key;
    m_sortAscending = ascending;
    beginResetModel();
    sortSource();
    rebuildView();
    endResetModel();
    emit sortChanged();
}

void FileListModel::setFilter(const QString &text)
{
    if (text == m_filter)
        return;
    m_filter = text;
    beginResetModel();
    rebuildView();
    endResetModel();
    emit filterChanged();
    emit countChanged();
}

void FileListModel::setEntries(QList<FileEntry> entries)
{
    beginResetModel();
    m_source = std::move(entries);
    sortSource();
    rebuildView();
    endResetModel();
    emit countChanged();
}

void FileListModel::clear()
{
    if (m_source.isEmpty() && m_entries.isEmpty())
        return;
    beginResetModel();
    m_source.clear();
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
