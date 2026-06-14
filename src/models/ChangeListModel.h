#pragma once

#include <QAbstractListModel>
#include <QList>

#include "engine/RsyncEvents.h"

// Streams parsed itemize changes into the QML view. A flat list is enough for
// M1; a tree (grouped by directory) can replace the backing store later without
// changing the role contract the QML delegate relies on.
class ChangeListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
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

public slots:
    void append(const ChangeItem &item);
    void clear();

signals:
    void countChanged();

private:
    QList<ChangeItem> m_items;
};
