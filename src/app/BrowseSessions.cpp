#include "app/BrowseSessions.h"

#include <utility>

BrowseSessions::BrowseSessions(RsyncCapabilities caps, SshHostStore hostStore, SecretStore secrets,
                               TransferManager *transfers, QObject *parent)
    : QObject(parent),
      m_caps(std::move(caps)),
      m_hostStore(std::move(hostStore)),
      m_secrets(std::move(secrets)),
      m_transfers(transfers)
{
    m_sessions.append(makeController());  // always at least one session
}

BrowseController *BrowseSessions::makeController()
{
    auto *c = new BrowseController(m_caps, m_hostStore, m_secrets, m_transfers, this);
    // A connect/disconnect changes the tab's label; a saved host should refresh the sidebar.
    connect(c, &BrowseController::targetChanged, this, &BrowseSessions::changed);
    connect(c, &BrowseController::connectedChanged, this, &BrowseSessions::changed);
    connect(c, &BrowseController::hostsChanged, this, &BrowseSessions::hostsChanged);
    return c;
}

BrowseController *BrowseSessions::current() const
{
    return m_sessions.isEmpty() ? nullptr : m_sessions.at(m_current);
}

QStringList BrowseSessions::labels() const
{
    QStringList out;
    out.reserve(m_sessions.size());
    for (const BrowseController *c : m_sessions)
        out << (c->connected() ? c->target() : QStringLiteral("New session"));
    return out;
}

void BrowseSessions::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_sessions.size() || index == m_current)
        return;
    m_current = index;
    emit currentChanged();
}

void BrowseSessions::addSession()
{
    m_sessions.append(makeController());
    m_current = static_cast<int>(m_sessions.size()) - 1;
    emit changed();
    emit currentChanged();
}

void BrowseSessions::closeSession(int index)
{
    if (index < 0 || index >= m_sessions.size() || m_sessions.size() <= 1)
        return;  // keep at least one session open

    BrowseController *c = m_sessions.takeAt(index);
    c->deleteLater();

    // Keep the active index valid and pointing at a sensible neighbour.
    if (m_current >= m_sessions.size())
        m_current = static_cast<int>(m_sessions.size()) - 1;
    else if (index < m_current)
        --m_current;

    emit changed();
    emit currentChanged();
}
