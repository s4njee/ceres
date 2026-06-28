#include "models/TransfersModel.h"

#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace {
QString cleanTreePath(QString path)
{
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (path.startsWith(QLatin1String("./")))
        path = path.mid(2);
    while (path.startsWith(QLatin1Char('/')))
        path = path.mid(1);
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    return path;
}
} // namespace

int TransfersModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant TransfersModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const Row &r = m_rows.at(index.row());
    switch (role) {
    case IdRole:
        return r.id;
    case NameRole:
        return r.name;
    case DirectionRole:
        return r.direction;
    case SourceRole:
        return r.source;
    case DestinationRole:
        return r.destination;
    case StatusRole:
        return static_cast<int>(r.status);
    case StatusTextRole:
        switch (r.status) {
        case Queued:
            return QStringLiteral("Queued");
        case Active:
            return QStringLiteral("Transferring…");
        case Done:
            return QStringLiteral("Done");
        case Failed:
            return QStringLiteral("Failed");
        case Cancelled:
            return QStringLiteral("Cancelled");
        case Paused:
            return QStringLiteral("Paused");
        }
        return {};
    case PercentRole:
        return r.percent;
    case SpeedRole:
        return r.speed;
    case SummaryRole:
        return r.summary;
    case ErrorRole:
        return r.error;
    case FileCountRole:
        return r.leafCount;
    case FilesRole: {
        // Cap the per-row tree handed to the (eager) QML Repeater so expanding a transfer
        // with tens of thousands of files stays responsive. The count role shows the true
        // total; this just bounds what's rendered.
        constexpr int kMaxRows = 2000;
        QVariantList list;
        const int n = qMin(r.files.size(), kMaxRows);
        list.reserve(n);
        for (int i = 0; i < n; ++i) {
            const FileLine &f = r.files.at(i);
            list.append(QVariantMap{{QStringLiteral("name"), f.name},
                                    {QStringLiteral("path"), f.path},
                                    {QStringLiteral("depth"), f.depth},
                                    {QStringLiteral("isDir"), f.isDir},
                                    {QStringLiteral("percent"), f.percent},
                                    {QStringLiteral("rate"), f.rate},
                                    {QStringLiteral("upToDate"), f.upToDate}});
        }
        return list;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> TransfersModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {DirectionRole, "direction"},
        {SourceRole, "source"},
        {DestinationRole, "destination"},
        {StatusRole, "status"},
        {StatusTextRole, "statusText"},
        {PercentRole, "percent"},
        {SpeedRole, "speed"},
        {SummaryRole, "summary"},
        {ErrorRole, "error"},
        {FilesRole, "files"},
        {FileCountRole, "fileCount"},
    };
}

int TransfersModel::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).id == id)
            return i;
    }
    return -1;
}

void TransfersModel::add(const QString &id, const QString &name, const QString &direction,
                         const QString &source, const QString &destination)
{
    const int row = static_cast<int>(m_rows.size());
    beginInsertRows(QModelIndex(), row, row);
    Row r;
    r.id = id;
    r.name = name;
    r.direction = direction;
    r.source = source;
    r.destination = destination;
    r.status = Queued;
    m_rows.append(r);
    endInsertRows();
    emit countChanged();
}

void TransfersModel::setStatus(const QString &id, Status status, const QString &error)
{
    const int idx = indexOfId(id);
    if (idx < 0)
        return;

    Row &r = m_rows[idx];
    if (r.status == status && r.error == error)
        return;

    r.status = status;
    r.error = error;
    const QModelIndex mi = index(idx);
    emit dataChanged(mi, mi);
    // Any status transition can move a row into or out of Active, so refresh the
    // tally; activeCount() recomputes from scratch so over-signalling is harmless.
    emit activeCountChanged();
}

void TransfersModel::updateProgress(const QString &id, int percent, const QString &speed)
{
    const int idx = indexOfId(id);
    if (idx < 0)
        return;

    Row &r = m_rows[idx];
    r.percent = percent;
    r.speed = speed;
    const QModelIndex mi = index(idx);
    emit dataChanged(mi, mi);
}

void TransfersModel::setSummary(const QString &id, const QString &summary)
{
    const int idx = indexOfId(id);
    if (idx < 0 || summary.isEmpty())
        return;
    Row &r = m_rows[idx];
    if (r.summary == summary)
        return;
    r.summary = summary;
    const QModelIndex mi = index(idx);
    emit dataChanged(mi, mi, {SummaryRole});
}

int TransfersModel::ensureFileLine(Row &r, const QString &cleanPath)
{
    const auto existing = r.fileIndex.constFind(cleanPath);
    if (existing != r.fileIndex.cend())
        return existing.value();

    const QStringList parts = cleanPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);

    // Synthesize any missing folder ancestors (O(1) membership via the index).
    QString prefix;
    for (int i = 0; i < parts.size() - 1; ++i) {
        if (!prefix.isEmpty())
            prefix += QLatin1Char('/');
        prefix += parts.at(i);
        const QString folderPath = prefix + QLatin1Char('/');
        if (!r.fileIndex.contains(folderPath)) {
            r.fileIndex.insert(folderPath, static_cast<int>(r.files.size()));
            r.files.append(FileLine{parts.at(i), folderPath, i, true, 100, QString(), false});
        }
    }

    const QString leafName = parts.isEmpty() ? cleanPath : parts.last();
    const int leafDepth = parts.isEmpty() ? 0 : static_cast<int>(parts.size() - 1);
    const int pos = static_cast<int>(r.files.size());
    r.fileIndex.insert(cleanPath, pos);
    r.files.append(FileLine{leafName, cleanPath, leafDepth, false, 0, QString(), false});
    ++r.leafCount;
    return pos;
}

void TransfersModel::updateFileProgress(const QString &id, const QString &path, int percent,
                                        const QString &rate)
{
    const int idx = indexOfId(id);
    const QString cleanPath = cleanTreePath(path);
    if (idx < 0 || cleanPath.isEmpty())
        return;

    Row &r = m_rows[idx];
    const int before = r.leafCount;
    const int pos = ensureFileLine(r, cleanPath);  // O(depth), not O(N)
    r.files[pos].percent = percent;
    r.files[pos].rate = rate;

    const QModelIndex mi = index(idx);
    emit dataChanged(mi, mi, r.leafCount == before ? QList<int>{FilesRole}
                                                   : QList<int>{FilesRole, FileCountRole});
}

void TransfersModel::seedFiles(const QString &id, const QStringList &paths)
{
    const int idx = indexOfId(id);
    if (idx < 0 || paths.isEmpty())
        return;

    Row &r = m_rows[idx];
    const int before = r.leafCount;

    // Sort so ancestors precede their children and the tree renders in a stable order.
    QStringList sorted = paths;
    sorted.sort();
    for (const QString &raw : sorted) {
        const QString cleanPath = cleanTreePath(raw);
        if (!cleanPath.isEmpty())
            ensureFileLine(r, cleanPath);  // creates at 0%, leaves an existing one untouched
    }

    const QModelIndex mi = index(idx);
    emit dataChanged(mi, mi, r.leafCount == before ? QList<int>{FilesRole}
                                                   : QList<int>{FilesRole, FileCountRole});
}

void TransfersModel::markUntouchedUpToDate(const QString &id)
{
    const int idx = indexOfId(id);
    if (idx < 0)
        return;

    Row &r = m_rows[idx];
    bool changed = false;
    for (FileLine &f : r.files) {
        // A leaf still at 0% after a clean finish was never sent — rsync skipped it
        // because the destination copy is already current. Show that, don't fail it.
        if (!f.isDir && !f.upToDate && f.percent == 0) {
            f.upToDate = true;
            f.percent = 100;
            changed = true;
        }
    }
    if (changed) {
        const QModelIndex mi = index(idx);
        emit dataChanged(mi, mi, {FilesRole});
    }
}

void TransfersModel::clearCompleted()
{
    // Walk from the end so removed indices don't shift the rows still to inspect.
    for (int i = static_cast<int>(m_rows.size()) - 1; i >= 0; --i) {
        const Status s = m_rows.at(i).status;
        if (s == Done || s == Failed || s == Cancelled) {
            beginRemoveRows(QModelIndex(), i, i);
            m_rows.removeAt(i);
            endRemoveRows();
        }
    }
    emit countChanged();
}

int TransfersModel::activeCount() const
{
    int n = 0;
    for (const Row &r : m_rows) {
        if (r.status == Active)
            ++n;
    }
    return n;
}
