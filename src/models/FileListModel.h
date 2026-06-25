#pragma once

#include <QAbstractListModel>
#include <QList>

#include "core/FileEntry.h"

/// Backs one pane of the browse tab's dual-pane file view. The same model type
/// drives both the local and remote panes; only the source of `setEntries()`
/// differs (QDir vs. RemoteFs). Directories sort before files, then by name.
///
/// `sizeText` is formatted here (KB/MB/GB) so the QML delegate stays presentation
/// only. `entryAt()` lets the controller resolve a selected row back to its name.
/// @ingroup models
class FileListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int sortKey READ sortKey NOTIFY sortChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending NOTIFY sortChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        IsDirRole,
        IsSymlinkRole,
        SizeRole,
        SizeTextRole,
        MtimeTextRole,
    };

    // Sort column. Directories always group ahead of files regardless of key.
    enum SortKey { ByName = 0, BySize, ByDate, ByType };
    Q_ENUM(SortKey)

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setEntries(QList<FileEntry> entries);  // applies the current sort
    void clear();

    int sortKey() const { return m_sortKey; }
    bool sortAscending() const { return m_sortAscending; }
    // Set the sort column/direction and re-sort in place. Clicking the same column
    // header twice (the QML pattern) toggles direction.
    Q_INVOKABLE void setSort(int key, bool ascending);

    QString filter() const { return m_filter; }
    // Case-insensitive substring filter on entry names; empty shows everything.
    void setFilter(const QString &text);

    const FileEntry &entryAt(int row) const { return m_entries.at(row); }
    bool isDirAt(int row) const;

    /// Human-readable byte count, e.g. 1536 -> "1.5 KB". Shared with tests.
    static QString humanSize(qint64 bytes);

signals:
    void countChanged();
    void sortChanged();
    void filterChanged();

private:
    void sortSource();    // sort m_source by the current key/direction (dirs first)
    void rebuildView();   // project m_source through the filter into m_entries

    QList<FileEntry> m_source;   // full directory listing (sorted)
    QList<FileEntry> m_entries;  // filtered view actually exposed to the list
    int m_sortKey = ByName;
    bool m_sortAscending = true;
    QString m_filter;
};
