#include "engine/OutputParser.h"

#include <QRegularExpression>
#include <QStringList>

namespace {

// Itemize line layout: 11-char flag field, one space, then the path.
//   >f+++++++++ newfile.txt
//   >f.st...... changed.txt
//   cd+++++++++ subdir/
//   *deleting   old.txt        (message reuses the 11-char field, left-justified)
bool looksLikeItemize(const QString &line)
{
    if (line.size() < 12 || line.at(11) != QLatin1Char(' '))
        return false;
    static const QString updateTypes = QStringLiteral("<>ch.*");
    if (!updateTypes.contains(line.at(0)))
        return false;
    if (line.at(0) == QLatin1Char('*'))
        return true;  // message form, e.g. *deleting
    static const QString fileTypes = QStringLiteral("fdLDS");
    return fileTypes.contains(line.at(1));
}

bool isStatsLine(const QString &line)
{
    static const QStringList prefixes = {
        QStringLiteral("Number of files"),
        QStringLiteral("Number of created files"),
        QStringLiteral("Number of deleted files"),
        QStringLiteral("Number of regular files transferred"),
        QStringLiteral("Total file size"),
        QStringLiteral("Total transferred file size"),
        QStringLiteral("Literal data"),
        QStringLiteral("Matched data"),
        QStringLiteral("File list size"),
        QStringLiteral("File list generation time"),
        QStringLiteral("File list transfer time"),
        QStringLiteral("Total bytes sent"),
        QStringLiteral("Total bytes received"),
        QStringLiteral("sent "),
        QStringLiteral("total size is "),
    };
    for (const QString &p : prefixes) {
        if (line.startsWith(p))
            return true;
    }
    return false;
}

qint64 parseRsyncQuantity(QString text)
{
    text.remove(QLatin1Char(','));

    bool ok = false;
    const qint64 exact = text.toLongLong(&ok);
    if (ok)
        return exact;

    static const QRegularExpression humanRe(QStringLiteral(R"(^(\d+(?:\.\d+)?)([KMGTPE]?)(?:B)?$)"));
    const auto m = humanRe.match(text);
    if (!m.hasMatch())
        return 0;

    const double value = m.captured(1).toDouble(&ok);
    if (!ok)
        return 0;

    double multiplier = 1.0;
    const QString suffix = m.captured(2);
    static const QString suffixes = QStringLiteral("KMGTPE");
    const int suffixIndex = suffixes.indexOf(suffix);
    for (int i = 0; i <= suffixIndex; ++i)
        multiplier *= 1024.0;

    return static_cast<qint64>(value * multiplier + 0.5);
}

} // namespace

OutputParser::OutputParser(QObject *parent) : QObject(parent) {}

void OutputParser::reset()
{
    m_buf.clear();
}

void OutputParser::feed(const QByteArray &data)
{
    m_buf += data;

    int pos = 0;
    while (true) {
        const int nl = m_buf.indexOf('\n', pos);
        const int cr = m_buf.indexOf('\r', pos);
        if (nl < 0 && cr < 0)
            break;  // no terminator yet; keep the remainder buffered

        if (nl >= 0 && (cr < 0 || nl < cr)) {
            // Plain \n line. Tolerate a trailing \r just before it.
            QByteArray raw = m_buf.mid(pos, nl - pos);
            if (raw.endsWith('\r'))
                raw.chop(1);
            handleLine(QString::fromUtf8(raw));
            pos = nl + 1;
        } else {
            // \r came first. Could be CRLF (a line) or a progress redraw.
            if (cr + 1 >= m_buf.size())
                break;  // can't tell yet — wait for the next byte
            if (m_buf.at(cr + 1) == '\n') {
                handleLine(QString::fromUtf8(m_buf.mid(pos, cr - pos)));
                pos = cr + 2;
            } else {
                handleProgress(QString::fromUtf8(m_buf.mid(pos, cr - pos)));
                pos = cr + 1;
            }
        }
    }

    if (pos > 0)
        m_buf.remove(0, pos);
}

void OutputParser::flush()
{
    if (m_buf.isEmpty())
        return;
    handleLine(QString::fromUtf8(m_buf));
    m_buf.clear();
}

void OutputParser::handleLine(const QString &line)
{
    if (line.isEmpty()) {
        emit log(line);
        return;
    }

    if (looksLikeItemize(line)) {
        ChangeItem it;
        const QString field = line.left(11);
        it.code = field.trimmed();
        it.path = line.mid(12);
        it.updateType = line.at(0);

        if (line.at(0) == QLatin1Char('*')) {
            it.op = it.code.contains(QStringLiteral("deleting")) ? ChangeItem::Op::Deletion
                                                                 : ChangeItem::Op::Message;
        } else {
            it.op = ChangeItem::Op::Update;
            it.fileType = line.at(1);
            it.isDir = (line.at(1) == QLatin1Char('d'));
            it.isNew = (field.mid(2, 9) == QStringLiteral("+++++++++"));
        }
        emit change(it);
        return;
    }

    if (isStatsLine(line)) {
        emit stats(line);
        return;
    }

    // The final 100% update is \n-terminated (not \r), so it arrives here.
    if (tryParseProgress(line))
        return;

    emit log(line);
}

void OutputParser::handleProgress(const QString &seg)
{
    const QString s = seg.trimmed();
    if (s.isEmpty())
        return;
    if (!tryParseProgress(s))
        emit log(s);  // not a progress redraw (could be an interim message)
}

bool OutputParser::tryParseProgress(const QString &text)
{
    const QString s = text.trimmed();
    if (s.isEmpty())
        return false;

    // e.g. "1,232,896 100%  117.55MB/s    0:00:00 (xfr#1, to-chk=4/6)"
    // With -h, the first field can be human-readable, e.g. "1.23M".
    static const QRegularExpression re(QStringLiteral(
        R"(^(\S+)\s+(\d+)%\s+(\S+)\s+(\d+:\d{2}:\d{2})(?:\s+\([^)]*(?:to-chk|ir-chk)=(\d+)/(\d+)[^)]*\))?)"));
    const auto m = re.match(s);
    if (!m.hasMatch())
        return false;

    ProgressInfo p;
    p.bytes = parseRsyncQuantity(m.captured(1));
    p.percent = m.captured(2).toInt();
    p.rate = m.captured(3);
    p.eta = m.captured(4);
    if (!m.captured(5).isEmpty()) {
        p.toCheck = m.captured(5).toInt();
        p.totalToCheck = m.captured(6).toInt();
    }
    // Pull xfr#N out separately so the to-chk group above stays simple.
    static const QRegularExpression xfrRe(QStringLiteral(R"(xfr#(\d+))"));
    const auto xm = xfrRe.match(s);
    if (xm.hasMatch())
        p.xfr = xm.captured(1).toInt();

    emit progress(p);
    return true;
}
