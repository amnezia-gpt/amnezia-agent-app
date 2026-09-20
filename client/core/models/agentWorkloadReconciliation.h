#ifndef AGENTWORKLOADRECONCILIATION_H
#define AGENTWORKLOADRECONCILIATION_H

#include "core/models/agentWorkloadDeploymentSpec.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

namespace amnezia
{
    inline constexpr qsizetype AgentWorkloadObservationMaxBytes = 256 * 1024;

    enum class AgentWorkloadObservationError {
        None,
        CommandFailed,
        OutputTooLarge,
        InvalidJson,
        UnsupportedSchemaVersion,
        InvalidEnvelope,
        TargetMismatch,
    };

    enum class AgentWorkloadHealth {
        None,
        Starting,
        Healthy,
        Unhealthy,
    };

    struct ObservedAgentWorkloadMount
    {
        QString type;
        QString name;
        QString destination;
    };

    struct ObservedAgentWorkloadPort
    {
        QString hostIp;
        QString hostPort;
        QString containerPort;
        QString protocol;
    };

    struct ObservedAgentWorkloadTmpfs
    {
        QString destination;
        QString options;
    };

    struct ObservedAgentWorkloadPortOwner
    {
        QString id;
        QString name;
    };

    struct ObservedDeploymentState
    {
        QString workload;
        QString targetContainerName;
        QString targetHostPort;
        bool containerPresent = false;
        QString containerId;
        QString containerName;
        QString imageReference;
        QString imageId;
        QString lifecycleStatus;
        bool running = false;
        AgentWorkloadHealth health = AgentWorkloadHealth::None;
        QString restartPolicy;
        QList<ObservedAgentWorkloadMount> mounts;
        QList<ObservedAgentWorkloadTmpfs> tmpfs;
        QStringList networks;
        QList<ObservedAgentWorkloadPort> ports;
        QMap<QString, QString> deploymentLabels;
        bool hostPortInUse = false;
        QList<ObservedAgentWorkloadPortOwner> portOwners;
    };

    struct AgentWorkloadObservationResult
    {
        AgentWorkloadObservationError error = AgentWorkloadObservationError::None;
        std::optional<ObservedDeploymentState> state;
    };

    enum class AgentWorkloadReconciliationAction {
        Create,
        NoOp,
        Start,
        Recreate,
        Conflict,
        Unknown,
    };

    enum class AgentWorkloadReconciliationReason {
        ContainerMissing,
        EquivalentAndRunning,
        EquivalentButStopped,
        DeclarationDrift,
        ImageDrift,
        RuntimeConfigurationDrift,
        ContainerOwnedByOther,
        HostPortInUse,
        ContainerUnhealthy,
        ContainerHealthPending,
        ContainerLifecycleIndeterminate,
        ObservationCommandFailed,
        ObservationOutputTooLarge,
        ObservationUnsupportedSchema,
        ObservationTargetMismatch,
        ObservationInvalid,
    };

    struct AgentWorkloadReconciliationPlan
    {
        AgentWorkloadReconciliationAction action = AgentWorkloadReconciliationAction::Unknown;
        AgentWorkloadReconciliationReason reason = AgentWorkloadReconciliationReason::ObservationInvalid;
    };

    AgentWorkloadObservationResult parseAgentWorkloadObservation(const QByteArray &payload,
                                                                 const AgentWorkloadDeploymentSpec &desired);
    AgentWorkloadObservationResult failedAgentWorkloadObservation(AgentWorkloadObservationError error);
    AgentWorkloadReconciliationPlan planAgentWorkloadReconciliation(const AgentWorkloadDeploymentSpec &desired,
                                                                    const AgentWorkloadObservationResult &observation);
    QString agentWorkloadReconciliationReasonText(AgentWorkloadReconciliationReason reason);

} // namespace amnezia

#endif // AGENTWORKLOADRECONCILIATION_H
