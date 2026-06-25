#pragma once

#include <QList>
#include <QObject>
#include <QStringList>

#include "app/BrowseController.h"
#include "core/SecretStore.h"
#include "core/SshHostStore.h"
#include "engine/BinaryLocator.h"

class TransferManager;

/// Owns a set of independent browse sessions (each a BrowseController with its own
/// SSH connection), backing the tabbed dual-pane browser. QML binds the panes to
/// `current`, so switching the active index re-points the whole UI at another
/// session; `labels` drives the tab strip. All sessions share the one TransferManager
/// and the host/secret stores. There is always at least one session.
/// @ingroup app
class BrowseSessions : public QObject {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentChanged)
    Q_PROPERTY(BrowseController *current READ current NOTIFY currentChanged)
    Q_PROPERTY(QStringList labels READ labels NOTIFY changed)
public:
    BrowseSessions(RsyncCapabilities caps, SshHostStore hostStore, SecretStore secrets,
                   TransferManager *transfers, QObject *parent = nullptr);

    int count() const { return static_cast<int>(m_sessions.size()); }
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int index);
    BrowseController *current() const;
    QStringList labels() const;  // one tab label per session ("New session" until connected)

    Q_INVOKABLE void addSession();          // append a fresh session and switch to it
    Q_INVOKABLE void closeSession(int index);  // close one (never the last remaining)

signals:
    void changed();         // session added/removed or a label changed
    void currentChanged();  // the active session switched
    void hostsChanged();    // relayed from any session so the sidebar can reload

private:
    BrowseController *makeController();

    RsyncCapabilities m_caps;
    SshHostStore m_hostStore;
    SecretStore m_secrets;
    TransferManager *m_transfers = nullptr;
    QList<BrowseController *> m_sessions;
    int m_current = 0;
};
