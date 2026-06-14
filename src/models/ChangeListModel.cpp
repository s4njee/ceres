#include "models/ChangeListModel.h"

int ChangeListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_items.size());
}

QVariant ChangeListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};

    const ChangeItem &it = m_items.at(index.row());
    switch (role) {
    case PathRole:
        return it.path;
    case CodeRole:
        return it.code;
    case OpRole:
        return static_cast<int>(it.op);
    case IsDirRole:
        return it.isDir;
    case IsNewRole:
        return it.isNew;
    case IsDeleteRole:
        return it.op == ChangeItem::Op::Deletion;
    default:
        return {};
    }
}

QHash<int, QByteArray> ChangeListModel::roleNames() const
{
    return {
        {PathRole, "path"},
        {CodeRole, "code"},
        {OpRole, "op"},
        {IsDirRole, "isDir"},
        {IsNewRole, "isNew"},
        {IsDeleteRole, "isDelete"},
    };
}

void ChangeListModel::append(const ChangeItem &item)
{
    ++m_totalCount;
    if (item.op == ChangeItem::Op::Deletion)
        ++m_deletions;

    if (m_items.size() >= m_maxItems) {
        beginRemoveRows(QModelIndex(), 0, 0);
        m_items.removeFirst();
        endRemoveRows();
    }

    const int row = static_cast<int>(m_items.size());
    beginInsertRows(QModelIndex(), row, row);
    m_items.append(item);
    endInsertRows();
    emit countChanged();
}

void ChangeListModel::clear()
{
    if (m_items.isEmpty() && m_totalCount == 0 && m_deletions == 0)
        return;
    beginResetModel();
    m_items.clear();
    m_totalCount = 0;
    m_deletions = 0;
    endResetModel();
    emit countChanged();
}
