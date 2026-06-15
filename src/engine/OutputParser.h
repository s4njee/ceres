#pragma once

#include <QByteArray>
#include <QObject>

#include "engine/RsyncEvents.h"

/// Incremental parser for rsync stdout. This is trickier than it looks because
/// rsync multiplexes two kinds of output on a single stdout stream:
///
///   1. `\n`-terminated lines: itemize changes, stats, and log messages
///   2. `\r`-terminated lines: progress2 redraws (carriage return, no newline)
///
/// QProcess delivers data in arbitrary byte chunks (not line-aligned), so the
/// parser buffers incoming bytes and scans for `\r` and `\n` terminators. A `\r\n`
/// pair is treated as a single line terminator (Windows compat). A lone `\r`
/// followed by a non-`\n` byte is a progress redraw.
///
/// Data flow: QProcess → feed() → handleLine()/handleProgress() → signals
/// The signals mirror SyncEngine's contract (change/progress/stats/log) and
/// are forwarded by RsyncProcessEngine.
/// @ingroup engine
class OutputParser : public QObject {
    Q_OBJECT
public:
    explicit OutputParser(QObject *parent = nullptr);

    void feed(const QByteArray &data);  // push a stdout chunk
    void flush();                       // emit any trailing buffered text as a line
    void reset();                       // drop buffered state between runs

signals:
    void change(const ChangeItem &item);
    void progress(const ProgressInfo &info);
    void stats(const QString &line);
    void log(const QString &line);

private:
    void handleLine(const QString &line);      // a complete \n-terminated line
    void handleProgress(const QString &seg);   // a \r-terminated redraw segment
    bool tryParseProgress(const QString &text); // parse+emit if it's a progress2 line

    QByteArray m_buf;
};
