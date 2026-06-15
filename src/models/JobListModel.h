#pragma once

#include <QAbstractListModel>
#include <QList>

#include "core/SyncJob.h"

/// Backs the sidebar list of saved sync profiles (jobs).
///
/// Populated from ProfileStore on startup and updated live when the user
/// saves, creates, or deletes a job. The QML ListView delegate binds to
/// roles like `name`, `summary`, and `id` to render the sidebar.
///
/// `upsert()` handles both creating a new job and updating an existing one
/// (matched by id), so callers don't need to distinguish insert from update.
/// @ingroup models
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
