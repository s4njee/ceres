#pragma once

#include <QAbstractListModel>
#include <QList>

#include "core/SyncJob.h"

// Backs the sidebar list of saved sync profiles.
class JobListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        SourceRole,
        DestinationRole,
        SummaryRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setJobs(const QList<SyncJob> &jobs);
    void upsert(const SyncJob &job);  // add, or replace the one with the same id
    void removeById(const QString &id);
    SyncJob jobById(const QString &id) const;

signals:
    void countChanged();

private:
    int indexOfId(const QString &id) const;

    QList<SyncJob> m_jobs;
};
