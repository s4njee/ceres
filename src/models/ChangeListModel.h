#pragma once

#include <QAbstractListModel>
#include <QList>

#include "engine/RsyncEvents.h"

/// Streams parsed itemize-changes events into the QML preview list view.
///
/// Each rsync change (file added, modified, deleted) arrives as a ChangeItem
/// from the engine and is appended here. The QML delegate binds to roles like
/// `path`, `code`, `isNew`, `isDelete` to colour-code each row.
///
/// To prevent unbounded memory growth on large syncs, the model caps its
/// displayed buffer at `m_maxItems` rows. The logical totals (`count` and
/// `deletions`) are still tracked accurately and shown in the UI header,
/// so the user always knows the true scope even if the oldest rows are dropped.
///
/// A flat list is enough for M1; a tree grouped by directory can replace the
/// backing store later without changing the role contract the QML delegate uses.
/// @ingroup models
class ChangeListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int deletions READ deletions NOTIFY countChanged)
public:
    enum Roles {
        PathRole = Qt::UserRole + 1,
        CodeRole,
        OpRole,
        IsDirRole,
        IsNewRole,
        IsDeleteRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_totalCount; }
    int deletions() const { return m_deletions; }

public slots:
    void append(const ChangeItem &item);
    void clear();

signals:
    void countChanged();

private:
    QList<ChangeItem> m_items;
    int m_totalCount = 0;
    int m_deletions = 0;
    // Keep the displayed buffer bounded for performance/memory; logical totals
    // (count + deletions) are still maintained and shown in the UI header.
    int m_maxItems = 20000;
};
