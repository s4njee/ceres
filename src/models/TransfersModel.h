#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

/// Backs the transfers modal: the live list of ad-hoc transfers managed by
/// TransferManager's parallel-capped queue.
///
/// The rows here are transient — one row
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
    // Appended (not reordered) so existing integer checks in QML stay valid.
    enum Status { Queued, Active, Done, Failed, Cancelled, Paused };
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
        FilesRole,      // QVariantList tree rows: { name, path, depth, isDir, percent, rate }
        FileCountRole,  // number of file rows recorded so far
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
    // Records/updates one transferred path. Parent folder rows are synthesized so
    // the UI can render a tree without a costly pre-transfer enumeration pass.
    void updateFileProgress(const QString &id, const QString &path, int percent,
                            const QString &rate);
    // Seeds the file tree with the full set of relative paths the transfer will
    // touch, each at 0%, so the user sees the whole list immediately rather than
    // one row appearing per file as rsync reaches it. Paths already present (a live
    // update can outrun the walk) are left untouched, never reset.
    void seedFiles(const QString &id, const QStringList &paths);
    // Called when a transfer finishes cleanly: any seeded leaf rsync never reported
    // (already up-to-date / skipped) is flagged so the UI distinguishes "skipped"
    // from "failed" instead of leaving it stuck at 0%.
    void markUntouchedUpToDate(const QString &id);

    // Removes Done/Failed/Cancelled rows; leaves Queued/Active untouched.
    Q_INVOKABLE void clearCompleted();

    int activeCount() const;  // number of rows currently in Active status

signals:
    void countChanged();
    void activeCountChanged();

private:
    struct FileLine {
        QString name;
        QString path;
        int depth = 0;
        bool isDir = false;
        int percent = 0;
        QString rate;
        bool upToDate = false;  // seeded but never transferred (already current / skipped)
    };
    struct Row {
        QString id, name, direction, source, destination, speed, error;
        Status status = Queued;
        int percent = 0;
        QList<FileLine> files;
    };

    int indexOfId(const QString &id) const;
    static int fileCount(const Row &row);

    QList<Row> m_rows;
};
