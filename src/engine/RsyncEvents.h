#pragma once

#include <QChar>
#include <QMetaType>
#include <QString>

// One parsed line of rsync --itemize-changes output (the YXcstpoguax format),
// or a message line such as "*deleting".
struct ChangeItem {
    enum class Op { Update, Deletion, Message };

    QString path;          // file/dir path (may contain spaces)
    QString code;          // trimmed itemize field, e.g. ">f+++++++++" or "*deleting"
    Op op = Op::Update;
    bool isDir = false;    // file-type column was 'd'
    bool isNew = false;    // all 9 attr columns are '+'
    QChar updateType;      // column 0: < > c h . *
    QChar fileType;        // column 1: f d L D S (null for messages)
};
Q_DECLARE_METATYPE(ChangeItem)

// One parsed `--info=progress2` aggregate line.
struct ProgressInfo {
    qint64 bytes = 0;       // bytes transferred so far
    int percent = 0;        // 0..100
    QString rate;           // e.g. "117.55MB/s"
    QString eta;            // e.g. "0:00:03"
    int xfr = -1;           // xfr#N  (-1 if absent)
    int toCheck = -1;       // to-chk numerator (files left)
    int totalToCheck = -1;  // to-chk denominator (total)
};
Q_DECLARE_METATYPE(ProgressInfo)
