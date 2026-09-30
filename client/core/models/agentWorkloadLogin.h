#ifndef AGENTWORKLOADLOGIN_H
#define AGENTWORKLOADLOGIN_H

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QUrl>

#include <optional>

#include "core/models/agentWorkloadReconciliation.h"
#include "core/utils/errorCodes.h"

namespace amnezia
{
    inline constexpr qsizetype AgentWorkloadLoginMaxBytes = 16 * 1024;

    enum class AgentWorkloadLoginMode {
        Native,
        Amgpt,
    };

    enum class AgentWorkloadLoginState {
        LoginRequired,
        Pending,
        Ready,
        Failed,
        Cancelled,
        Expired,
        Denied,
        RuntimeUnavailable,
    };

    enum class AgentWorkloadLoginRemoteError {
        None,
        InvalidRequest,
        UnsupportedMode,
        ProviderUnavailable,
        MalformedProviderResponse,
        StartFailed,
        StartTimeout,
        BackgroundFailed,
        Cancelled,
        Expired,
        Denied,
        SupervisorUnavailable,
        SupervisorTimeout,
        SupervisorResponseTooLarge,
        SupervisorMalformedResponse,
        Unknown,
    };

    enum class AgentWorkloadLoginParseError {
        None,
        OutputTooLarge,
        InvalidJson,
        UnsupportedSchemaVersion,
        InvalidEnvelope,
        ModeMismatch,
        InvalidField,
        UnsafeVerificationUrl,
        RemoteFailure,
    };

    enum class AgentWorkloadLoginOperationError {
        None,
        InvalidTarget,
        WorkloadNotReady,
        ObservationFailed,
        CommandFailed,
        OutputTooLarge,
        InvalidResponse,
        RemoteFailure,
    };

    enum class AgentWorkloadLoginPrecondition {
        None,
        InvalidRequest,
        DeviceGatewayMissing,
        DeviceGatewayNotReady,
    };

    struct AgentWorkloadLoginPresentation
    {
        AgentWorkloadLoginMode mode = AgentWorkloadLoginMode::Native;
        QUrl verificationUrl;
        std::optional<QUrl> completeVerificationUrl;
        QString userCode;
        std::optional<int> expiresInSeconds;

        QUrl preferredUrl() const;
    };

    struct AgentWorkloadLoginStatus
    {
        AgentWorkloadLoginMode mode = AgentWorkloadLoginMode::Native;
        AgentWorkloadLoginState state = AgentWorkloadLoginState::Failed;
        bool authenticated = false;
        AgentWorkloadLoginRemoteError remoteError = AgentWorkloadLoginRemoteError::None;
    };

    struct AgentWorkloadLoginStartParseResult
    {
        AgentWorkloadLoginParseError error = AgentWorkloadLoginParseError::InvalidEnvelope;
        AgentWorkloadLoginRemoteError remoteError = AgentWorkloadLoginRemoteError::None;
        std::optional<AgentWorkloadLoginPresentation> presentation;
    };

    struct AgentWorkloadLoginStatusParseResult
    {
        AgentWorkloadLoginParseError error = AgentWorkloadLoginParseError::InvalidEnvelope;
        std::optional<AgentWorkloadLoginStatus> status;
    };

    struct AgentWorkloadLoginStartResult
    {
        AgentWorkloadLoginOperationError error = AgentWorkloadLoginOperationError::InvalidTarget;
        AgentWorkloadLoginParseError parseError = AgentWorkloadLoginParseError::None;
        AgentWorkloadLoginRemoteError remoteError = AgentWorkloadLoginRemoteError::None;
        ErrorCode transportError = ErrorCode::NoError;
        std::optional<AgentWorkloadLoginPresentation> presentation;
    };

    struct AgentWorkloadLoginStatusResult
    {
        AgentWorkloadLoginOperationError error = AgentWorkloadLoginOperationError::InvalidTarget;
        AgentWorkloadLoginParseError parseError = AgentWorkloadLoginParseError::None;
        ErrorCode transportError = ErrorCode::NoError;
        std::optional<AgentWorkloadLoginStatus> status;
    };

    QString agentWorkloadLoginModeName(AgentWorkloadLoginMode mode);
    AgentWorkloadLoginStartParseResult parseAgentWorkloadLoginStart(const QByteArray &payload,
                                                                    AgentWorkloadLoginMode expectedMode);
    AgentWorkloadLoginStatusParseResult parseAgentWorkloadLoginStatus(const QByteArray &payload,
                                                                      AgentWorkloadLoginMode expectedMode);
    bool isSafeAgentWorkloadVerificationUrl(const QUrl &url);
} // namespace amnezia

Q_DECLARE_METATYPE(amnezia::AgentWorkloadLoginMode)
Q_DECLARE_METATYPE(amnezia::AgentWorkloadLoginParseError)

#endif // AGENTWORKLOADLOGIN_H
