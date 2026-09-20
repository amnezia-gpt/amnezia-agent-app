#include "agentWorkloadLogin.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

namespace amnezia
{
    namespace
    {
        const QSet<QString> &startSuccessKeys()
        {
            static const QSet<QString> keys {
                QStringLiteral("schema_version"), QStringLiteral("mode"),
                QStringLiteral("state"), QStringLiteral("verification_uri"),
                QStringLiteral("verification_uri_complete"), QStringLiteral("user_code"),
                QStringLiteral("expires_in"), QStringLiteral("error_code"),
            };
            return keys;
        }

        const QSet<QString> &statusKeys()
        {
            static const QSet<QString> keys {
                QStringLiteral("schema_version"), QStringLiteral("mode"), QStringLiteral("state"),
                QStringLiteral("authenticated"), QStringLiteral("error_code"),
            };
            return keys;
        }

        bool hasExactKeys(const QJsonObject &object, const QSet<QString> &expected)
        {
            const QStringList keys = object.keys();
            return QSet<QString>(keys.cbegin(), keys.cend()) == expected;
        }

        bool hasControlCharacters(const QString &value)
        {
            for (const QChar character : value) {
                if (character.unicode() < 0x20 || character.unicode() == 0x7f) {
                    return true;
                }
            }
            return false;
        }

        std::optional<QJsonObject> parseEnvelope(const QByteArray &payload, AgentWorkloadLoginParseError &error)
        {
            if (payload.size() > AgentWorkloadLoginMaxBytes) {
                error = AgentWorkloadLoginParseError::OutputTooLarge;
                return std::nullopt;
            }
            QJsonParseError jsonError;
            const QJsonDocument document = QJsonDocument::fromJson(payload, &jsonError);
            if (jsonError.error != QJsonParseError::NoError || !document.isObject()) {
                error = AgentWorkloadLoginParseError::InvalidJson;
                return std::nullopt;
            }
            const QJsonObject object = document.object();
            if (!object.value(QStringLiteral("schema_version")).isDouble()
                || object.value(QStringLiteral("schema_version")).toInt(-1) != 1) {
                error = AgentWorkloadLoginParseError::UnsupportedSchemaVersion;
                return std::nullopt;
            }
            return object;
        }

        std::optional<AgentWorkloadLoginMode> parseMode(const QJsonValue &value)
        {
            if (!value.isString()) {
                return std::nullopt;
            }
            if (value.toString() == QStringLiteral("native")) {
                return AgentWorkloadLoginMode::Native;
            }
            if (value.toString() == QStringLiteral("amgpt")) {
                return AgentWorkloadLoginMode::Amgpt;
            }
            return std::nullopt;
        }

        AgentWorkloadLoginRemoteError parseRemoteError(const QJsonValue &value)
        {
            if (!value.isString()) {
                return AgentWorkloadLoginRemoteError::Unknown;
            }
            const QString code = value.toString();
            if (code == QStringLiteral("invalid-request")) return AgentWorkloadLoginRemoteError::InvalidRequest;
            if (code == QStringLiteral("unsupported-mode")) return AgentWorkloadLoginRemoteError::UnsupportedMode;
            if (code == QStringLiteral("provider-unavailable")) return AgentWorkloadLoginRemoteError::ProviderUnavailable;
            if (code == QStringLiteral("malformed-provider-response")) return AgentWorkloadLoginRemoteError::MalformedProviderResponse;
            if (code == QStringLiteral("start-failed")) return AgentWorkloadLoginRemoteError::StartFailed;
            if (code == QStringLiteral("start-timeout")) return AgentWorkloadLoginRemoteError::StartTimeout;
            if (code == QStringLiteral("background-failed")) return AgentWorkloadLoginRemoteError::BackgroundFailed;
            if (code == QStringLiteral("cancelled")) return AgentWorkloadLoginRemoteError::Cancelled;
            if (code == QStringLiteral("expired")) return AgentWorkloadLoginRemoteError::Expired;
            if (code == QStringLiteral("denied")) return AgentWorkloadLoginRemoteError::Denied;
            if (code == QStringLiteral("supervisor-unavailable")) return AgentWorkloadLoginRemoteError::SupervisorUnavailable;
            if (code == QStringLiteral("supervisor-timeout")) return AgentWorkloadLoginRemoteError::SupervisorTimeout;
            if (code == QStringLiteral("supervisor-response-too-large")) return AgentWorkloadLoginRemoteError::SupervisorResponseTooLarge;
            if (code == QStringLiteral("supervisor-malformed-response")) return AgentWorkloadLoginRemoteError::SupervisorMalformedResponse;
            return AgentWorkloadLoginRemoteError::Unknown;
        }

        std::optional<AgentWorkloadLoginState> parseState(const QJsonValue &value)
        {
            if (!value.isString()) return std::nullopt;
            const QString state = value.toString();
            if (state == QStringLiteral("login-required")) return AgentWorkloadLoginState::LoginRequired;
            if (state == QStringLiteral("pending")) return AgentWorkloadLoginState::Pending;
            if (state == QStringLiteral("ready")) return AgentWorkloadLoginState::Ready;
            if (state == QStringLiteral("failed")) return AgentWorkloadLoginState::Failed;
            if (state == QStringLiteral("cancelled")) return AgentWorkloadLoginState::Cancelled;
            if (state == QStringLiteral("expired")) return AgentWorkloadLoginState::Expired;
            if (state == QStringLiteral("denied")) return AgentWorkloadLoginState::Denied;
            if (state == QStringLiteral("runtime-unavailable")) return AgentWorkloadLoginState::RuntimeUnavailable;
            return std::nullopt;
        }

        bool validRemoteFailure(const QJsonObject &object, AgentWorkloadLoginMode expectedMode,
                                AgentWorkloadLoginRemoteError &remoteError)
        {
            const auto mode = parseMode(object.value(QStringLiteral("mode")));
            if ((!object.value(QStringLiteral("mode")).isNull() && (!mode || *mode != expectedMode))
                || object.value(QStringLiteral("state")).toString() != QStringLiteral("failed")
                || !object.value(QStringLiteral("authenticated")).isBool()
                || object.value(QStringLiteral("authenticated")).toBool()) {
                return false;
            }
            remoteError = parseRemoteError(object.value(QStringLiteral("error_code")));
            return remoteError != AgentWorkloadLoginRemoteError::Unknown;
        }
    } // namespace

    QUrl AgentWorkloadLoginPresentation::preferredUrl() const
    {
        return completeVerificationUrl.value_or(verificationUrl);
    }

    QString agentWorkloadLoginModeName(AgentWorkloadLoginMode mode)
    {
        return mode == AgentWorkloadLoginMode::Native ? QStringLiteral("native") : QStringLiteral("amgpt");
    }

    bool isSafeAgentWorkloadVerificationUrl(const QUrl &url)
    {
        return url.toString(QUrl::FullyEncoded).size() <= 4096
                && url.isValid() && !url.isRelative() && url.scheme() == QStringLiteral("https") && !url.host().isEmpty()
                && url.userInfo().isEmpty() && !url.hasFragment()
                && !hasControlCharacters(url.toString(QUrl::FullyEncoded));
    }

    AgentWorkloadLoginPrecondition evaluateAgentWorkloadLoginPrecondition(
            AgentWorkloadLoginMode mode, const AgentWorkloadReconciliationPlan &openClaw,
            const std::optional<AgentWorkloadReconciliationPlan> &authProxy)
    {
        if (openClaw.action != AgentWorkloadReconciliationAction::NoOp) {
            return AgentWorkloadLoginPrecondition::OpenClawNotReady;
        }
        if (mode == AgentWorkloadLoginMode::Native) {
            return AgentWorkloadLoginPrecondition::None;
        }
        if (!authProxy) {
            return AgentWorkloadLoginPrecondition::AuthProxyMissing;
        }
        if (authProxy->action != AgentWorkloadReconciliationAction::NoOp) {
            return AgentWorkloadLoginPrecondition::AuthProxyNotReady;
        }
        return AgentWorkloadLoginPrecondition::None;
    }

    AgentWorkloadLoginStartParseResult parseAgentWorkloadLoginStart(const QByteArray &payload,
                                                                    AgentWorkloadLoginMode expectedMode)
    {
        AgentWorkloadLoginStartParseResult result;
        const auto envelope = parseEnvelope(payload, result.error);
        if (!envelope) return result;
        const QJsonObject &object = *envelope;
        if (object.value(QStringLiteral("state")).toString() == QStringLiteral("failed")) {
            if (!hasExactKeys(object, statusKeys()) || !validRemoteFailure(object, expectedMode, result.remoteError)) {
                result.error = AgentWorkloadLoginParseError::InvalidEnvelope;
                return result;
            }
            result.error = AgentWorkloadLoginParseError::RemoteFailure;
            return result;
        }
        if (!hasExactKeys(object, startSuccessKeys())) {
            result.error = AgentWorkloadLoginParseError::InvalidEnvelope;
            return result;
        }
        const auto mode = parseMode(object.value(QStringLiteral("mode")));
        if (!mode || *mode != expectedMode) {
            result.error = AgentWorkloadLoginParseError::ModeMismatch;
            return result;
        }
        if (object.value(QStringLiteral("state")).toString() != QStringLiteral("pending")
            || !object.value(QStringLiteral("error_code")).isNull()) {
            result.error = AgentWorkloadLoginParseError::InvalidField;
            return result;
        }
        const QJsonValue uriValue = object.value(QStringLiteral("verification_uri"));
        const QJsonValue completeValue = object.value(QStringLiteral("verification_uri_complete"));
        const QJsonValue codeValue = object.value(QStringLiteral("user_code"));
        const QJsonValue expiryValue = object.value(QStringLiteral("expires_in"));
        if (!uriValue.isString() || (!completeValue.isString() && !completeValue.isNull()) || !codeValue.isString()
            || (!expiryValue.isDouble() && !expiryValue.isNull()) || codeValue.toString().isEmpty()
            || codeValue.toString().size() > 256 || hasControlCharacters(codeValue.toString())) {
            result.error = AgentWorkloadLoginParseError::InvalidField;
            return result;
        }
        AgentWorkloadLoginPresentation presentation;
        presentation.mode = *mode;
        presentation.verificationUrl = QUrl::fromEncoded(uriValue.toString().toUtf8(), QUrl::StrictMode);
        if (!isSafeAgentWorkloadVerificationUrl(presentation.verificationUrl)) {
            result.error = AgentWorkloadLoginParseError::UnsafeVerificationUrl;
            return result;
        }
        if (completeValue.isString()) {
            const QUrl complete = QUrl::fromEncoded(completeValue.toString().toUtf8(), QUrl::StrictMode);
            if (!isSafeAgentWorkloadVerificationUrl(complete)) {
                result.error = AgentWorkloadLoginParseError::UnsafeVerificationUrl;
                return result;
            }
            presentation.completeVerificationUrl = complete;
        }
        presentation.userCode = codeValue.toString();
        if (expiryValue.isDouble()) {
            const double raw = expiryValue.toDouble();
            const int seconds = expiryValue.toInt(-1);
            if (raw != seconds || seconds <= 0) {
                result.error = AgentWorkloadLoginParseError::InvalidField;
                return result;
            }
            presentation.expiresInSeconds = seconds;
        }
        result.error = AgentWorkloadLoginParseError::None;
        result.presentation = presentation;
        return result;
    }

    AgentWorkloadLoginStatusParseResult parseAgentWorkloadLoginStatus(const QByteArray &payload,
                                                                      AgentWorkloadLoginMode expectedMode)
    {
        AgentWorkloadLoginStatusParseResult result;
        const auto envelope = parseEnvelope(payload, result.error);
        if (!envelope) return result;
        const QJsonObject &object = *envelope;
        if (!hasExactKeys(object, statusKeys())) {
            result.error = AgentWorkloadLoginParseError::InvalidEnvelope;
            return result;
        }
        if (object.value(QStringLiteral("mode")).isNull()) {
            AgentWorkloadLoginRemoteError remoteError;
            if (validRemoteFailure(object, expectedMode, remoteError)) {
                result.error = AgentWorkloadLoginParseError::None;
                result.status = AgentWorkloadLoginStatus { expectedMode, AgentWorkloadLoginState::Failed, false, remoteError };
                return result;
            }
        }
        const auto mode = parseMode(object.value(QStringLiteral("mode")));
        const auto state = parseState(object.value(QStringLiteral("state")));
        if (!mode || *mode != expectedMode) {
            result.error = AgentWorkloadLoginParseError::ModeMismatch;
            return result;
        }
        if (!state || !object.value(QStringLiteral("authenticated")).isBool()) {
            result.error = AgentWorkloadLoginParseError::InvalidField;
            return result;
        }
        const bool authenticated = object.value(QStringLiteral("authenticated")).toBool();
        if (authenticated != (*state == AgentWorkloadLoginState::Ready)) {
            result.error = AgentWorkloadLoginParseError::InvalidField;
            return result;
        }
        AgentWorkloadLoginRemoteError remoteError = AgentWorkloadLoginRemoteError::None;
        const QJsonValue errorValue = object.value(QStringLiteral("error_code"));
        if (!errorValue.isNull()) {
            remoteError = parseRemoteError(errorValue);
            if (remoteError == AgentWorkloadLoginRemoteError::Unknown) {
                result.error = AgentWorkloadLoginParseError::InvalidField;
                return result;
            }
        }
        result.error = AgentWorkloadLoginParseError::None;
        result.status = AgentWorkloadLoginStatus { *mode, *state, authenticated, remoteError };
        return result;
    }

} // namespace amnezia
