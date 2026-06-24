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
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        IsDirRole,
        IsSymlinkRole,
        SizeRole,
        SizeTextRole,
        MtimeTextRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setEntries(QList<FileEntry> entries);  // sorts dirs-first, then by name
    void clear();

    const FileEntry &entryAt(int row) const { return m_entries.at(row); }
    bool isDirAt(int row) const;

    /// Human-readable byte count, e.g. 1536 -> "1.5 KB". Shared with tests.
    static QString humanSize(qint64 bytes);

signals:
    void countChanged();

private:
    QList<FileEntry> m_entries;
};
