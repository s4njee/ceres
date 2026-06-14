#pragma once

#include <QByteArray>
#include <QObject>

#include "engine/RsyncEvents.h"

// Incremental parser for rsync stdout. rsync mixes \r-redrawn progress lines
// and \n-terminated itemize/stats lines on ONE stream, and QProcess delivers
// arbitrary byte chunks, so feed() buffers and only emits on a real
// terminator. Splits on both \r and \n; handles \r\n as a line terminator.
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
