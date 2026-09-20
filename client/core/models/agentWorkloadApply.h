#ifndef AGENTWORKLOADAPPLY_H
#define AGENTWORKLOADAPPLY_H

#include "core/models/agentWorkloadDeploymentSpec.h"
#include "core/models/agentWorkloadReconciliation.h"
#include "core/utils/errorCodes.h"

#include <QString>

#include <optional>

namespace amnezia
{
    inline constexpr qsizetype AgentWorkloadApplyOutputMaxBytes = 8 * 1024;
    inline constexpr auto AgentWorkloadApplySuccessMarker = "AMNEZIA_AGENT_APPLY_APPLIED";
    inline constexpr auto AgentWorkloadApplyFailureMarker = "AMNEZIA_AGENT_APPLY_FAILED";
    inline constexpr auto AgentWorkloadApplyHealthTimeoutMarker = "AMNEZIA_AGENT_APPLY_HEALTH_TIMEOUT";

    enum class AgentWorkloadApplyRenderError {
        None,
        InvalidDesiredState,
        UnsupportedAction,
        MissingPlaybook,
        UnresolvedPlaceholder,
    };

    enum class AgentWorkloadApplyStatus {
        Applied,
        NoOp,
        Failed,
        Unknown,
    };

    enum class AgentWorkloadApplyReason {
        None,
        InvalidDesiredState,
        UnsupportedPlan,
        PreflightObservationFailed,
        StalePlan,
        RenderFailed,
        CommandFailed,
        OutputTooLarge,
        MissingTerminalMarker,
        HealthTimeout,
        ScriptFailed,
        PostApplyObservationFailed,
        PostApplyDrift,
    };

    struct AgentWorkloadApplyResult
    {
        AgentWorkloadApplyStatus status = AgentWorkloadApplyStatus::Failed;
        AgentWorkloadApplyReason reason = AgentWorkloadApplyReason::None;
        ErrorCode transportError = ErrorCode::NoError;
    };

    enum class AgentWorkloadLifecycleAction {
        Stop,
        Remove,
    };

    enum class AgentWorkloadLifecycleStatus {
        Applied,
        NoOp,
        Conflict,
        Failed,
        Unknown,
    };

    enum class AgentWorkloadLifecycleReason {
        None,
        InvalidDesiredState,
        ObservationFailed,
        ContainerMissing,
        AlreadyStopped,
        ContainerOwnedByOther,
        CommandFailed,
        PostMutationObservationFailed,
        PostMutationMismatch,
    };

    struct AgentWorkloadLifecycleResult
    {
        AgentWorkloadLifecycleStatus status = AgentWorkloadLifecycleStatus::Failed;
        AgentWorkloadLifecycleReason reason = AgentWorkloadLifecycleReason::None;
        ErrorCode transportError = ErrorCode::NoError;
    };

    std::optional<QString> renderAgentWorkloadApplyScript(const AgentWorkloadDeploymentSpec &spec,
                                                          AgentWorkloadReconciliationAction action,
                                                          AgentWorkloadApplyRenderError *error = nullptr);
    QString agentWorkloadApplyCommand(const QString &renderedScript);

} // namespace amnezia

#endif // AGENTWORKLOADAPPLY_H
