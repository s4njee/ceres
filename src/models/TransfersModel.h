#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

/// Backs the transfers modal: the live list of ad-hoc transfers managed by
/// TransferManager's parallel-capped queue.
///
/// Unlike JobListModel (saved profiles), the rows here are transient — one row
/// per in-flight or recently-finished transfer. TransferManager owns this model
/// and is the only writer; it calls add() to enqueue a row, then setStatus() /
/// updateProgress() as each engine reports lifecycle and progress. The QML
/// ListView delegate binds to roles like `name`, `statusText`, `percent`, and
/// `speed` to render each transfer's state.
///
/// Rows are addressed by the transfer's id (not by index) because the manager
/// tracks transfers by id and rows may be removed out of order by
/// clearCompleted(); indexOfId() does the lookup. `activeCount` is exposed
/// separately so the UI can show "N transferring" without scanning the model.
/// @ingroup models
class TransfersModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY activeCountChanged)
public:
    enum Status { Queued, Active, Done, Failed, Cancelled };
    Q_ENUM(Status)

    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        DirectionRole,
        SourceRole,
        DestinationRole,
        StatusRole,
        StatusTextRole,
        PercentRole,
        SpeedRole,
        ErrorRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Inserts a new Queued row. `direction` is "up" or "down".
    void add(const QString &id, const QString &name, const QString &direction,
             const QString &source, const QString &destination);
    void setStatus(const QString &id, Status status, const QString &error = {});
    void updateProgress(const QString &id, int percent, const QString &speed);

    // Removes Done/Failed/Cancelled rows; leaves Queued/Active untouched.
    Q_INVOKABLE void clearCompleted();

    int activeCount() const;  // number of rows currently in Active status

signals:
    void countChanged();
    void activeCountChanged();

private:
    struct Row {
        QString id, name, direction, source, destination, speed, error;
        Status status = Queued;
        int percent = 0;
    };

    int indexOfId(const QString &id) const;

    QList<Row> m_rows;
};
