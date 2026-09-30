#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>

#include "core/models/agentWorkloadLogin.h"
#include "core/models/agentWorkloadReconciliation.h"

using namespace amnezia;

namespace
{
    QByteArray startPayload(const QString &mode = QStringLiteral("native"))
    {
        return QJsonDocument(QJsonObject {
                                     { QStringLiteral("schema_version"), 1 },
                                     { QStringLiteral("mode"), mode },
                                     { QStringLiteral("state"), QStringLiteral("pending") },
                                     { QStringLiteral("verification_uri"),
                                       QStringLiteral("https://auth.example/device") },
                                     { QStringLiteral("verification_uri_complete"), QJsonValue::Null },
                                     { QStringLiteral("user_code"), QStringLiteral("ABCD-EFGH") },
                                     { QStringLiteral("expires_in"), QJsonValue::Null },
                                     { QStringLiteral("error_code"), QJsonValue::Null },
                             })
                .toJson(QJsonDocument::Compact);
    }

    QByteArray statusPayload(const QString &mode, const QString &state, bool authenticated,
                             const QJsonValue &errorCode = QJsonValue::Null)
    {
        return QJsonDocument(QJsonObject {
                                     { QStringLiteral("schema_version"), 1 },
                                     { QStringLiteral("mode"), mode },
                                     { QStringLiteral("state"), state },
                                     { QStringLiteral("authenticated"), authenticated },
                                     { QStringLiteral("error_code"), errorCode },
                             })
                .toJson(QJsonDocument::Compact);
    }
} // namespace

class AgentWorkloadLoginTest : public QObject
{
    Q_OBJECT

private slots:
    void parsesNativePresentationWithNullableExpiry();
    void prefersValidatedCompleteUrl();
    void rejectsUntrustedPresentation_data();
    void rejectsUntrustedPresentation();
    void parsesBoundedRemoteFailureWithoutProviderText();
    void acceptsUnattributedRemoteFailure();
    void parsesStatusStatesAndRejectsContradictions();
};

void AgentWorkloadLoginTest::parsesNativePresentationWithNullableExpiry()
{
    const auto result = parseAgentWorkloadLoginStart(startPayload(), AgentWorkloadLoginMode::Native);

    QCOMPARE(result.error, AgentWorkloadLoginParseError::None);
    QVERIFY(result.presentation);
    QCOMPARE(result.presentation->mode, AgentWorkloadLoginMode::Native);
    QCOMPARE(result.presentation->verificationUrl, QUrl(QStringLiteral("https://auth.example/device")));
    QCOMPARE(result.presentation->userCode, QStringLiteral("ABCD-EFGH"));
    QVERIFY(!result.presentation->expiresInSeconds);
}

void AgentWorkloadLoginTest::prefersValidatedCompleteUrl()
{
    QJsonObject object = QJsonDocument::fromJson(startPayload(QStringLiteral("amgpt"))).object();
    object.insert(QStringLiteral("verification_uri_complete"),
                  QStringLiteral("https://auth.example/device?user_code=ABCD-EFGH"));
    object.insert(QStringLiteral("expires_in"), 600);

    const auto result = parseAgentWorkloadLoginStart(QJsonDocument(object).toJson(QJsonDocument::Compact),
                                                     AgentWorkloadLoginMode::Amgpt);

    QCOMPARE(result.error, AgentWorkloadLoginParseError::None);
    QVERIFY(result.presentation);
    QCOMPARE(result.presentation->preferredUrl().toString(QUrl::FullyEncoded),
             QStringLiteral("https://auth.example/device?user_code=ABCD-EFGH"));
    QCOMPARE(result.presentation->expiresInSeconds, std::optional<int>(600));
}

void AgentWorkloadLoginTest::rejectsUntrustedPresentation_data()
{
    QTest::addColumn<QByteArray>("payload");
    QTest::addColumn<AgentWorkloadLoginParseError>("expected");

    QTest::newRow("truncated") << QByteArray("{\"schema_version\":1")
                                << AgentWorkloadLoginParseError::InvalidJson;
    QTest::newRow("trailing-document") << startPayload() + QByteArray("\n{}")
                                        << AgentWorkloadLoginParseError::InvalidJson;

    QJsonObject wrongSchema = QJsonDocument::fromJson(startPayload()).object();
    wrongSchema.insert(QStringLiteral("schema_version"), 2);
    QTest::newRow("schema") << QJsonDocument(wrongSchema).toJson(QJsonDocument::Compact)
                             << AgentWorkloadLoginParseError::UnsupportedSchemaVersion;

    QJsonObject wrongMode = QJsonDocument::fromJson(startPayload()).object();
    wrongMode.insert(QStringLiteral("mode"), QStringLiteral("amgpt"));
    QTest::newRow("mode") << QJsonDocument(wrongMode).toJson(QJsonDocument::Compact)
                           << AgentWorkloadLoginParseError::ModeMismatch;

    QJsonObject http = QJsonDocument::fromJson(startPayload()).object();
    http.insert(QStringLiteral("verification_uri"), QStringLiteral("http://auth.example/device"));
    QTest::newRow("http") << QJsonDocument(http).toJson(QJsonDocument::Compact)
                           << AgentWorkloadLoginParseError::UnsafeVerificationUrl;

    for (const QString &field : { QStringLiteral("verification_uri"), QStringLiteral("verification_uri_complete") }) {
        QJsonObject oversizedUrl = QJsonDocument::fromJson(startPayload()).object();
        oversizedUrl.insert(field, QStringLiteral("https://auth.example/") + QString(4096, QLatin1Char('a')));
        QTest::newRow(qPrintable(field)) << QJsonDocument(oversizedUrl).toJson(QJsonDocument::Compact)
                                       << AgentWorkloadLoginParseError::UnsafeVerificationUrl;
    }

    QJsonObject userInfo = QJsonDocument::fromJson(startPayload()).object();
    userInfo.insert(QStringLiteral("verification_uri"), QStringLiteral("https://user:pass@auth.example/device"));
    QTest::newRow("userinfo") << QJsonDocument(userInfo).toJson(QJsonDocument::Compact)
                               << AgentWorkloadLoginParseError::UnsafeVerificationUrl;

    QJsonObject fragment = QJsonDocument::fromJson(startPayload()).object();
    fragment.insert(QStringLiteral("verification_uri_complete"), QStringLiteral("https://auth.example/device#code"));
    QTest::newRow("fragment") << QJsonDocument(fragment).toJson(QJsonDocument::Compact)
                               << AgentWorkloadLoginParseError::UnsafeVerificationUrl;

    QJsonObject control = QJsonDocument::fromJson(startPayload()).object();
    control.insert(QStringLiteral("user_code"), QStringLiteral("ABCD\nEFGH"));
    QTest::newRow("control-character") << QJsonDocument(control).toJson(QJsonDocument::Compact)
                                        << AgentWorkloadLoginParseError::InvalidField;

    QJsonObject expiry = QJsonDocument::fromJson(startPayload()).object();
    expiry.insert(QStringLiteral("expires_in"), 0);
    QTest::newRow("expiry") << QJsonDocument(expiry).toJson(QJsonDocument::Compact)
                             << AgentWorkloadLoginParseError::InvalidField;

    QJsonObject unexpected = QJsonDocument::fromJson(startPayload()).object();
    unexpected.insert(QStringLiteral("access_token"), QStringLiteral("must-not-cross-boundary"));
    QTest::newRow("unexpected-field") << QJsonDocument(unexpected).toJson(QJsonDocument::Compact)
                                       << AgentWorkloadLoginParseError::InvalidEnvelope;

    QTest::newRow("oversized") << QByteArray(AgentWorkloadLoginMaxBytes + 1, 'x')
                                << AgentWorkloadLoginParseError::OutputTooLarge;
}

void AgentWorkloadLoginTest::rejectsUntrustedPresentation()
{
    QFETCH(QByteArray, payload);
    QFETCH(AgentWorkloadLoginParseError, expected);

    const auto result = parseAgentWorkloadLoginStart(payload, AgentWorkloadLoginMode::Native);

    QCOMPARE(result.error, expected);
    QVERIFY(!result.presentation);
}

void AgentWorkloadLoginTest::parsesBoundedRemoteFailureWithoutProviderText()
{
    const QByteArray payload = QJsonDocument(QJsonObject {
                                                     { QStringLiteral("schema_version"), 1 },
                                                     { QStringLiteral("mode"), QStringLiteral("amgpt") },
                                                     { QStringLiteral("state"), QStringLiteral("failed") },
                                                     { QStringLiteral("authenticated"), false },
                                                     { QStringLiteral("error_code"),
                                                       QStringLiteral("provider-unavailable") },
                                             })
                                       .toJson(QJsonDocument::Compact);

    const auto result = parseAgentWorkloadLoginStart(payload, AgentWorkloadLoginMode::Amgpt);

    QCOMPARE(result.error, AgentWorkloadLoginParseError::RemoteFailure);
    QCOMPARE(result.remoteError, AgentWorkloadLoginRemoteError::ProviderUnavailable);
    QVERIFY(!result.presentation);
}

void AgentWorkloadLoginTest::acceptsUnattributedRemoteFailure()
{
    auto object = QJsonDocument::fromJson(statusPayload(QStringLiteral("native"), QStringLiteral("failed"),
                                                       false, QStringLiteral("supervisor-unavailable"))).object();
    object.insert(QStringLiteral("mode"), QJsonValue::Null);
    const auto payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    const auto start = parseAgentWorkloadLoginStart(payload, AgentWorkloadLoginMode::Native);
    QCOMPARE(start.error, AgentWorkloadLoginParseError::RemoteFailure);
    QCOMPARE(start.remoteError, AgentWorkloadLoginRemoteError::SupervisorUnavailable);
    const auto status = parseAgentWorkloadLoginStatus(payload, AgentWorkloadLoginMode::Native);
    QCOMPARE(status.error, AgentWorkloadLoginParseError::None);
    QVERIFY(status.status);
    QCOMPARE(status.status->state, AgentWorkloadLoginState::Failed);
    QVERIFY(!status.status->authenticated);
}

void AgentWorkloadLoginTest::parsesStatusStatesAndRejectsContradictions()
{
    const auto ready = parseAgentWorkloadLoginStatus(
            statusPayload(QStringLiteral("native"), QStringLiteral("ready"), true),
            AgentWorkloadLoginMode::Native);
    QCOMPARE(ready.error, AgentWorkloadLoginParseError::None);
    QVERIFY(ready.status);
    QCOMPARE(ready.status->state, AgentWorkloadLoginState::Ready);
    QVERIFY(ready.status->authenticated);

    const auto contradiction = parseAgentWorkloadLoginStatus(
            statusPayload(QStringLiteral("native"), QStringLiteral("pending"), true),
            AgentWorkloadLoginMode::Native);
    QCOMPARE(contradiction.error, AgentWorkloadLoginParseError::InvalidField);
    QVERIFY(!contradiction.status);
}

QTEST_APPLESS_MAIN(AgentWorkloadLoginTest)

#include "agent_workload_login_test.moc"
