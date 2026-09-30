#include "agentWorkloadReconciliation.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

namespace amnezia
{
    namespace
    {
        constexpr auto LabelPrefix = "org.amnezia.amgpt.deployment.";
        constexpr auto ManagedByLabel = "org.amnezia.amgpt.deployment.managed-by";
        constexpr auto WorkloadLabel = "org.amnezia.amgpt.deployment.workload";
        constexpr auto BackendProfileLabel = "org.amnezia.amgpt.deployment.backend-profile";
        constexpr auto ExpectedManager = "amnezia-agent-app";

        const QSet<QString> &allowedDeploymentLabels()
        {
            static const QSet<QString> labels {
                QStringLiteral("org.amnezia.amgpt.deployment.managed-by"),
                QStringLiteral("org.amnezia.amgpt.deployment.workload"),
                QStringLiteral("org.amnezia.amgpt.deployment.schema-version"),
                QStringLiteral("org.amnezia.amgpt.deployment.workload-version"),
                QStringLiteral("org.amnezia.amgpt.deployment.spec-hash"),
                QStringLiteral("org.amnezia.amgpt.deployment.backend-profile"),
            };
            return labels;
        }

        AgentWorkloadObservationResult failure(AgentWorkloadObservationError error)
        {
            return { error, std::nullopt };
        }

        bool requiredString(const QJsonObject &object, const QString &key, QString *target)
        {
            const QJsonValue value = object.value(key);
            if (!value.isString() || value.toString().isEmpty()) {
                return false;
            }
            *target = value.toString();
            return true;
        }

        std::optional<AgentWorkloadHealth> parseHealth(const QJsonValue &value)
        {
            if (!value.isString()) {
                return std::nullopt;
            }
            const QString health = value.toString();
            if (health == QStringLiteral("none")) {
                return AgentWorkloadHealth::None;
            }
            if (health == QStringLiteral("starting")) {
                return AgentWorkloadHealth::Starting;
            }
            if (health == QStringLiteral("healthy")) {
                return AgentWorkloadHealth::Healthy;
            }
            if (health == QStringLiteral("unhealthy")) {
                return AgentWorkloadHealth::Unhealthy;
            }
            return std::nullopt;
        }

        bool isKnownLifecycle(const QString &status)
        {
            static const QSet<QString> known {
                QStringLiteral("created"),    QStringLiteral("running"),  QStringLiteral("paused"),
                QStringLiteral("restarting"), QStringLiteral("removing"), QStringLiteral("exited"),
                QStringLiteral("dead"),
            };
            return known.contains(status);
        }

        bool lifecycleMatchesRunning(const QString &status, bool running)
        {
            const bool reportsRunning = status == QStringLiteral("running") || status == QStringLiteral("paused")
                    || status == QStringLiteral("restarting");
            return reportsRunning == running;
        }

        bool parseMounts(const QJsonValue &value, QList<ObservedAgentWorkloadMount> *target)
        {
            if (!value.isArray()) {
                return false;
            }
            QSet<QString> identities;
            for (const QJsonValue &entry : value.toArray()) {
                if (!entry.isObject()) {
                    return false;
                }
                ObservedAgentWorkloadMount mount;
                const QJsonObject object = entry.toObject();
                if (!requiredString(object, QStringLiteral("type"), &mount.type)
                    || !requiredString(object, QStringLiteral("destination"), &mount.destination)) {
                    return false;
                }
                const QJsonValue name = object.value(QStringLiteral("name"));
                if (!name.isString()) {
                    return false;
                }
                mount.name = name.toString();
                const QString identity =
                        mount.type + QLatin1Char('\n') + mount.name + QLatin1Char('\n') + mount.destination;
                if (identities.contains(identity)) {
                    return false;
                }
                identities.insert(identity);
                target->append(mount);
            }
            return true;
        }

        bool parseNetworks(const QJsonValue &value, QStringList *target)
        {
            if (!value.isArray()) {
                return false;
            }
            QSet<QString> seen;
            for (const QJsonValue &entry : value.toArray()) {
                if (!entry.isString() || entry.toString().isEmpty() || seen.contains(entry.toString())) {
                    return false;
                }
                seen.insert(entry.toString());
                target->append(entry.toString());
            }
            return true;
        }

        bool parseTmpfs(const QJsonValue &value, QList<ObservedAgentWorkloadTmpfs> *target)
        {
            if (!value.isArray()) {
                return false;
            }
            QSet<QString> destinations;
            for (const QJsonValue &entry : value.toArray()) {
                if (!entry.isObject()) {
                    return false;
                }
                ObservedAgentWorkloadTmpfs tmpfs;
                const QJsonObject object = entry.toObject();
                if (!requiredString(object, QStringLiteral("destination"), &tmpfs.destination)
                    || !requiredString(object, QStringLiteral("options"), &tmpfs.options)
                    || destinations.contains(tmpfs.destination)) {
                    return false;
                }
                destinations.insert(tmpfs.destination);
                target->append(tmpfs);
            }
            return true;
        }

        bool parsePorts(const QJsonValue &value, QList<ObservedAgentWorkloadPort> *target)
        {
            if (!value.isArray()) {
                return false;
            }
            QSet<QString> identities;
            for (const QJsonValue &entry : value.toArray()) {
                if (!entry.isObject()) {
                    return false;
                }
                ObservedAgentWorkloadPort port;
                const QJsonObject object = entry.toObject();
                if (!requiredString(object, QStringLiteral("host_ip"), &port.hostIp)
                    || !requiredString(object, QStringLiteral("host_port"), &port.hostPort)
                    || !requiredString(object, QStringLiteral("container_port"), &port.containerPort)
                    || !requiredString(object, QStringLiteral("protocol"), &port.protocol)) {
                    return false;
                }
                bool hostPortValid = false;
                bool containerPortValid = false;
                const int hostPort = port.hostPort.toInt(&hostPortValid);
                const int containerPort = port.containerPort.toInt(&containerPortValid);
                if (!hostPortValid || !containerPortValid || hostPort < 1 || hostPort > 65535 || containerPort < 1
                    || containerPort > 65535 || port.hostPort != QString::number(hostPort)
                    || port.containerPort != QString::number(containerPort)
                    || (port.protocol != QStringLiteral("tcp") && port.protocol != QStringLiteral("udp"))) {
                    return false;
                }
                const QString identity = port.hostIp + QLatin1Char('\n') + port.hostPort + QLatin1Char('\n')
                        + port.containerPort + QLatin1Char('\n') + port.protocol;
                if (identities.contains(identity)) {
                    return false;
                }
                identities.insert(identity);
                target->append(port);
            }
            return true;
        }

        bool parseLabels(const QJsonValue &value, QMap<QString, QString> *target)
        {
            if (!value.isObject()) {
                return false;
            }
            const QJsonObject object = value.toObject();
            for (auto it = object.begin(); it != object.end(); ++it) {
                if (!it.key().startsWith(QString::fromLatin1(LabelPrefix))) {
                    continue;
                }
                if (!allowedDeploymentLabels().contains(it.key()) || !it.value().isString()) {
                    return false;
                }
                target->insert(it.key(), it.value().toString());
            }
            return true;
        }

        bool parsePortOwners(const QJsonValue &value, QList<ObservedAgentWorkloadPortOwner> *target)
        {
            if (!value.isArray()) {
                return false;
            }
            QSet<QString> ids;
            for (const QJsonValue &entry : value.toArray()) {
                if (!entry.isObject()) {
                    return false;
                }
                ObservedAgentWorkloadPortOwner owner;
                const QJsonObject object = entry.toObject();
                if (!requiredString(object, QStringLiteral("id"), &owner.id)
                    || !requiredString(object, QStringLiteral("name"), &owner.name) || ids.contains(owner.id)) {
                    return false;
                }
                ids.insert(owner.id);
                target->append(owner);
            }
            return true;
        }

        QSet<QString> desiredMounts(const AgentWorkloadDeploymentSpec &desired)
        {
            QSet<QString> result;
            for (const AgentWorkloadVolume &volume : desired.volumes) {
                result.insert(QStringLiteral("volume\n") + volume.name + QLatin1Char('\n') + volume.target);
            }
            return result;
        }

        QSet<QString> observedMounts(const ObservedDeploymentState &observed)
        {
            QSet<QString> result;
            for (const auto &mount : observed.mounts) {
                result.insert(mount.type + QLatin1Char('\n') + mount.name + QLatin1Char('\n') + mount.destination);
            }
            return result;
        }

        QSet<QString> desiredTmpfs(const AgentWorkloadDeploymentSpec &desired)
        {
            QSet<QString> result;
            for (const QString &entry : desired.tmpfs) {
                const qsizetype separator = entry.indexOf(QLatin1Char(':'));
                if (separator <= 0 || separator == entry.size() - 1) {
                    return {};
                }
                result.insert(entry.left(separator) + QLatin1Char('\n') + entry.mid(separator + 1));
            }
            return result;
        }

        QSet<QString> observedTmpfs(const ObservedDeploymentState &observed)
        {
            QSet<QString> result;
            for (const auto &tmpfs : observed.tmpfs) {
                result.insert(tmpfs.destination + QLatin1Char('\n') + tmpfs.options);
            }
            return result;
        }

        bool runtimeConfigurationMatches(const AgentWorkloadDeploymentSpec &desired,
                                         const ObservedDeploymentState &observed)
        {
            if (observed.restartPolicy != desired.restartPolicy || observedMounts(observed) != desiredMounts(desired)
                || observedTmpfs(observed) != desiredTmpfs(desired)
                || QSet<QString>(observed.networks.cbegin(), observed.networks.cend())
                        != QSet<QString> { desired.networkName }) {
                return false;
            }
            if (desired.hostPort.isEmpty()) {
                return observed.ports.isEmpty();
            }
            if (observed.ports.isEmpty()) {
                return false;
            }
            QSet<QString> addresses;
            for (const auto &port : observed.ports) {
                if (port.hostPort != desired.hostPort || port.containerPort != desired.containerPort
                    || port.protocol != QStringLiteral("tcp")
                    || (port.hostIp != QStringLiteral("0.0.0.0") && port.hostIp != QStringLiteral("::"))
                    || addresses.contains(port.hostIp)) {
                    return false;
                }
                addresses.insert(port.hostIp);
            }
            return true;
        }

        AgentWorkloadReconciliationReason reasonForObservationError(AgentWorkloadObservationError error)
        {
            switch (error) {
            case AgentWorkloadObservationError::CommandFailed:
                return AgentWorkloadReconciliationReason::ObservationCommandFailed;
            case AgentWorkloadObservationError::OutputTooLarge:
                return AgentWorkloadReconciliationReason::ObservationOutputTooLarge;
            case AgentWorkloadObservationError::UnsupportedSchemaVersion:
                return AgentWorkloadReconciliationReason::ObservationUnsupportedSchema;
            case AgentWorkloadObservationError::TargetMismatch:
                return AgentWorkloadReconciliationReason::ObservationTargetMismatch;
            case AgentWorkloadObservationError::None:
            case AgentWorkloadObservationError::InvalidJson:
            case AgentWorkloadObservationError::InvalidEnvelope:
                return AgentWorkloadReconciliationReason::ObservationInvalid;
            }
            return AgentWorkloadReconciliationReason::ObservationInvalid;
        }
    } // namespace

    AgentWorkloadObservationResult parseAgentWorkloadObservation(const QByteArray &payload,
                                                                 const AgentWorkloadDeploymentSpec &desired)
    {
        if (payload.size() > AgentWorkloadObservationMaxBytes) {
            return failure(AgentWorkloadObservationError::OutputTooLarge);
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            return failure(AgentWorkloadObservationError::InvalidJson);
        }
        const QJsonObject root = document.object();
        const QJsonValue schemaVersion = root.value(QStringLiteral("schema_version"));
        if (!schemaVersion.isDouble() || schemaVersion.toDouble() != 1.0) {
            return failure(schemaVersion.isDouble() ? AgentWorkloadObservationError::UnsupportedSchemaVersion
                                                    : AgentWorkloadObservationError::InvalidEnvelope);
        }
        if (!root.value(QStringLiteral("target")).isObject() || !root.value(QStringLiteral("host_port_in_use")).isBool()) {
            return failure(AgentWorkloadObservationError::InvalidEnvelope);
        }

        ObservedDeploymentState state;
        const QJsonObject target = root.value(QStringLiteral("target")).toObject();
        // An unpublished workload is observed with an empty target port.
        const QJsonValue targetHostPort = target.value(QStringLiteral("host_port"));
        if (!requiredString(target, QStringLiteral("workload"), &state.workload)
            || !requiredString(target, QStringLiteral("container_name"), &state.targetContainerName)
            || !targetHostPort.isString()) {
            return failure(AgentWorkloadObservationError::InvalidEnvelope);
        }
        state.targetHostPort = targetHostPort.toString();
        if (state.workload != desired.workload || state.targetContainerName != desired.containerName
            || state.targetHostPort != desired.hostPort) {
            return failure(AgentWorkloadObservationError::TargetMismatch);
        }
        state.hostPortInUse = root.value(QStringLiteral("host_port_in_use")).toBool();
        if (!parsePortOwners(root.value(QStringLiteral("port_owners")), &state.portOwners)) {
            return failure(AgentWorkloadObservationError::InvalidEnvelope);
        }

        const QJsonValue containerValue = root.value(QStringLiteral("container"));
        if (containerValue.isNull()) {
            for (const auto &owner : state.portOwners) {
                if (owner.name == desired.containerName) {
                    return failure(AgentWorkloadObservationError::InvalidEnvelope);
                }
            }
            state.containerPresent = false;
            return { AgentWorkloadObservationError::None, state };
        }
        if (!containerValue.isObject()) {
            return failure(AgentWorkloadObservationError::InvalidEnvelope);
        }

        state.containerPresent = true;
        const QJsonObject container = containerValue.toObject();
        const QJsonValue running = container.value(QStringLiteral("running"));
        const auto health = parseHealth(container.value(QStringLiteral("health")));
        if (!requiredString(container, QStringLiteral("id"), &state.containerId)
            || !requiredString(container, QStringLiteral("name"), &state.containerName)
            || !requiredString(container, QStringLiteral("image_reference"), &state.imageReference)
            || !requiredString(container, QStringLiteral("image_id"), &state.imageId)
            || !requiredString(container, QStringLiteral("status"), &state.lifecycleStatus) || !running.isBool()
            || !health || !requiredString(container, QStringLiteral("restart_policy"), &state.restartPolicy)
            || !parseMounts(container.value(QStringLiteral("mounts")), &state.mounts)
            || !parseTmpfs(container.value(QStringLiteral("tmpfs")), &state.tmpfs)
            || !parseNetworks(container.value(QStringLiteral("networks")), &state.networks)
            || !parsePorts(container.value(QStringLiteral("ports")), &state.ports)
            || !parseLabels(container.value(QStringLiteral("labels")), &state.deploymentLabels)) {
            return failure(AgentWorkloadObservationError::InvalidEnvelope);
        }
        state.running = running.toBool();
        state.health = *health;
        if (state.containerName.startsWith(QLatin1Char('/'))) {
            state.containerName.remove(0, 1);
        }
        if (state.containerName != desired.containerName || !isKnownLifecycle(state.lifecycleStatus)
            || !lifecycleMatchesRunning(state.lifecycleStatus, state.running)) {
            return failure(state.containerName != desired.containerName ? AgentWorkloadObservationError::TargetMismatch
                                                                        : AgentWorkloadObservationError::InvalidEnvelope);
        }

        return { AgentWorkloadObservationError::None, state };
    }

    AgentWorkloadObservationResult failedAgentWorkloadObservation(AgentWorkloadObservationError error)
    {
        return error == AgentWorkloadObservationError::None ? failure(AgentWorkloadObservationError::InvalidEnvelope)
                                                            : failure(error);
    }

    AgentWorkloadReconciliationPlan planAgentWorkloadReconciliation(const AgentWorkloadDeploymentSpec &desired,
                                                                    const AgentWorkloadObservationResult &observation)
    {
        if (observation.error != AgentWorkloadObservationError::None || !observation.state) {
            return { AgentWorkloadReconciliationAction::Unknown, reasonForObservationError(observation.error) };
        }

        const ObservedDeploymentState &state = *observation.state;
        if (!state.containerPresent) {
            if (state.hostPortInUse || !state.portOwners.isEmpty()) {
                return { AgentWorkloadReconciliationAction::Conflict, AgentWorkloadReconciliationReason::HostPortInUse };
            }
            return { AgentWorkloadReconciliationAction::Create, AgentWorkloadReconciliationReason::ContainerMissing };
        }

        const QString managedBy = state.deploymentLabels.value(QString::fromLatin1(ManagedByLabel));
        const QString workload = state.deploymentLabels.value(QString::fromLatin1(WorkloadLabel));
        if (managedBy != QString::fromLatin1(ExpectedManager) || workload != desired.workload) {
            return { AgentWorkloadReconciliationAction::Conflict,
                     AgentWorkloadReconciliationReason::ContainerOwnedByOther };
        }
        bool targetOwnsRequestedPort = false;
        for (const auto &owner : state.portOwners) {
            if (owner.id != state.containerId) {
                return { AgentWorkloadReconciliationAction::Conflict, AgentWorkloadReconciliationReason::HostPortInUse };
            }
            targetOwnsRequestedPort = true;
        }
        if (state.hostPortInUse && !targetOwnsRequestedPort) {
            return { AgentWorkloadReconciliationAction::Conflict, AgentWorkloadReconciliationReason::HostPortInUse };
        }
        if (state.imageReference != desired.imageReference) {
            return { AgentWorkloadReconciliationAction::Recreate, AgentWorkloadReconciliationReason::ImageDrift };
        }

        const QMap<QString, QString> desiredLabels = desired.deploymentLabels();
        for (auto it = desiredLabels.cbegin(); it != desiredLabels.cend(); ++it) {
            if (state.deploymentLabels.value(it.key()) != it.value()) {
                return { AgentWorkloadReconciliationAction::Recreate,
                         AgentWorkloadReconciliationReason::DeclarationDrift };
            }
        }
        if (!desired.backendProfile && state.deploymentLabels.contains(QString::fromLatin1(BackendProfileLabel))) {
            return { AgentWorkloadReconciliationAction::Recreate, AgentWorkloadReconciliationReason::DeclarationDrift };
        }
        if (!runtimeConfigurationMatches(desired, state)) {
            return { AgentWorkloadReconciliationAction::Recreate,
                     AgentWorkloadReconciliationReason::RuntimeConfigurationDrift };
        }
        if (!state.running) {
            if (state.lifecycleStatus == QStringLiteral("created") || state.lifecycleStatus == QStringLiteral("exited")) {
                return { AgentWorkloadReconciliationAction::Start,
                         AgentWorkloadReconciliationReason::EquivalentButStopped };
            }
            return { AgentWorkloadReconciliationAction::Unknown,
                     AgentWorkloadReconciliationReason::ContainerLifecycleIndeterminate };
        }
        if (state.lifecycleStatus != QStringLiteral("running")) {
            return { AgentWorkloadReconciliationAction::Unknown,
                     AgentWorkloadReconciliationReason::ContainerLifecycleIndeterminate };
        }
        if (state.health == AgentWorkloadHealth::Starting || state.health == AgentWorkloadHealth::None) {
            return { AgentWorkloadReconciliationAction::Unknown,
                     AgentWorkloadReconciliationReason::ContainerHealthPending };
        }
        if (state.health == AgentWorkloadHealth::Unhealthy) {
            return { AgentWorkloadReconciliationAction::Recreate, AgentWorkloadReconciliationReason::ContainerUnhealthy };
        }
        return { AgentWorkloadReconciliationAction::NoOp, AgentWorkloadReconciliationReason::EquivalentAndRunning };
    }

    QString agentWorkloadReconciliationReasonText(AgentWorkloadReconciliationReason reason)
    {
        switch (reason) {
        case AgentWorkloadReconciliationReason::ContainerMissing: return QStringLiteral("Container is not installed");
        case AgentWorkloadReconciliationReason::EquivalentAndRunning:
            return QStringLiteral("Container matches the requested deployment and is running");
        case AgentWorkloadReconciliationReason::EquivalentButStopped:
            return QStringLiteral("Container matches the requested deployment but is stopped");
        case AgentWorkloadReconciliationReason::DeclarationDrift:
            return QStringLiteral("Container deployment declaration differs from the requested deployment");
        case AgentWorkloadReconciliationReason::ImageDrift:
            return QStringLiteral("Container image differs from the requested release");
        case AgentWorkloadReconciliationReason::RuntimeConfigurationDrift:
            return QStringLiteral("Container runtime configuration differs from the requested deployment");
        case AgentWorkloadReconciliationReason::ContainerOwnedByOther:
            return QStringLiteral("The container name is owned by another deployment");
        case AgentWorkloadReconciliationReason::HostPortInUse:
            return QStringLiteral("The requested host port is already in use");
        case AgentWorkloadReconciliationReason::ContainerUnhealthy:
            return QStringLiteral("Container reports an unhealthy state");
        case AgentWorkloadReconciliationReason::ContainerHealthPending:
            return QStringLiteral("Container health is not yet known");
        case AgentWorkloadReconciliationReason::ContainerLifecycleIndeterminate:
            return QStringLiteral("Container lifecycle is changing");
        case AgentWorkloadReconciliationReason::ObservationCommandFailed:
            return QStringLiteral("Remote container inspection failed");
        case AgentWorkloadReconciliationReason::ObservationOutputTooLarge:
            return QStringLiteral("Remote container inspection returned too much data");
        case AgentWorkloadReconciliationReason::ObservationUnsupportedSchema:
            return QStringLiteral("Remote container inspection uses an unsupported schema");
        case AgentWorkloadReconciliationReason::ObservationTargetMismatch:
            return QStringLiteral("Remote container inspection returned a different target");
        case AgentWorkloadReconciliationReason::ObservationInvalid:
            return QStringLiteral("Remote container inspection returned invalid data");
        }
        return QStringLiteral("Remote container state is unknown");
    }

} // namespace amnezia
