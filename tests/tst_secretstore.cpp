#include <QtTest>

#include <QUuid>

#include "core/SecretStore.h"

// Exercises the real OS secret backend (Windows Credential Manager, macOS
// Keychain, libsecret). Where no backend is available (e.g. a headless Linux
// build without a secret service), set() returns false and the test skips
// rather than failing.
class SecretStoreTest : public QObject {
    Q_OBJECT
private slots:
    void roundTrip();
    void cleanup();

private:
    SecretStore m_store;
    // A unique id per run so a crashed prior run can't collide, and so we never
    // touch a real job's stored password.
    const QString m_id = QStringLiteral("ceres-selftest-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
};

void SecretStoreTest::roundTrip()
{
    // Whitespace and non-ASCII to prove the value is stored verbatim (no
    // trimming, correct UTF-8 round-trip).
    const QString secret = QStringLiteral("  s3cr3t pä$$ /\\ \t end  ");

    if (!m_store.set(m_id, secret))
        QSKIP("No OS secret backend available on this platform/build");

    QCOMPARE(m_store.get(m_id), secret);

    // Overwriting an existing entry must replace, not duplicate or fail.
    const QString updated = QStringLiteral("second-value");
    QVERIFY(m_store.set(m_id, updated));
    QCOMPARE(m_store.get(m_id), updated);

    QVERIFY(m_store.remove(m_id));
    QVERIFY(m_store.get(m_id).isEmpty());

    // Removing a missing entry is a no-op success.
    QVERIFY(m_store.remove(m_id));
}

void SecretStoreTest::cleanup()
{
    m_store.remove(m_id);  // best-effort: never leave a test secret behind
}

QTEST_MAIN(SecretStoreTest)
#include "tst_secretstore.moc"
