#pragma once

#include <QChar>
#include <QMetaType>
#include <QString>

/// @file RsyncEvents.h
/// Value types emitted by OutputParser and relayed through SyncEngine signals.
/// These are the "events" that flow from a running rsync process up to the UI:
///   ChangeItem   — one parsed `--itemize-changes` line (file added/modified/deleted)
///   ProgressInfo — one parsed `--info=progress2` aggregate progress line
///
/// They are registered with Q_DECLARE_METATYPE so they can be passed through
/// queued signal/slot connections (e.g. across thread boundaries).

/// One parsed line of rsync --itemize-changes output (the YXcstpoguax format),
/// or a message line such as "*deleting".
///
/// The 11-character itemize field encodes what changed about each file:
///   Column 0: update type (<, >, c, h, ., *)
///   Column 1: file type   (f=file, d=dir, L=symlink, D=device, S=special)
///   Columns 2-10: attribute flags (c=checksum, s=size, t=time, p=perms, etc.)
///   All '+' in columns 2-10 means the file is newly created.
/// @ingroup engine
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

/// One parsed `--info=progress2` aggregate line.
/// @ingroup engine
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
