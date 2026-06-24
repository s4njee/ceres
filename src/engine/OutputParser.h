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

    /// Per-file mode parses rsync's `--progress` output (used for transfers): each
    /// progress redraw is the *current file's* own percent rather than the whole-run
    /// aggregate. In this mode the parser still emits an aggregate `progress()`, but
    /// derives it from the completed-file count plus the current file's fraction.
    /// Off by default (the sync tab uses `--info=progress2` aggregate).
    void setPerFileProgress(bool on) { m_perFile = on; }

signals:
    void change(const ChangeItem &item);
    void progress(const ProgressInfo &info);
    void fileProgress(const QString &path, int percent, const QString &rate);
    void stats(const QString &line);
    void log(const QString &line);

private:
    void handleLine(const QString &line);      // a complete \n-terminated line
    void handleProgress(const QString &seg);   // a \r-terminated redraw segment
    bool tryParseProgress(const QString &text); // parse+emit if it's a progress line

    QByteArray m_buf;

    // Per-file mode state (see setPerFileProgress).
    bool m_perFile = false;
    QString m_currentFile;   // last itemized file rsync started sending
    int m_curPercent = 0;    // current file's percent (reset to 0 as each file finishes)
    int m_filesDone = 0;     // files completed so far (from to-chk)
    int m_totalFiles = 0;    // total files in the run (from to-chk denominator)
};
