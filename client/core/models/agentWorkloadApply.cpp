#include "agentWorkloadApply.h"

#include "core/utils/containerEnum.h"
#include "core/utils/selfhosted/scriptsRegistry.h"

#include <QMap>
#include <QRegularExpression>

#include <algorithm>

namespace amnezia
{
    namespace
    {
        void setError(AgentWorkloadApplyRenderError *target, AgentWorkloadApplyRenderError error)
        {
            if (target) {
                *target = error;
            }
        }

        QString shellQuote(const QString &value)
        {
            QString quoted = value;
            quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
            return QLatin1Char('\'') + quoted + QLatin1Char('\'');
        }

        QString actionName(AgentWorkloadReconciliationAction action)
        {
            switch (action) {
            case AgentWorkloadReconciliationAction::Create: return QStringLiteral("create");
            case AgentWorkloadReconciliationAction::Start: return QStringLiteral("start");
            case AgentWorkloadReconciliationAction::Recreate: return QStringLiteral("recreate");
            case AgentWorkloadReconciliationAction::NoOp:
            case AgentWorkloadReconciliationAction::Conflict:
            case AgentWorkloadReconciliationAction::Unknown: return {};
            }
            return {};
        }

        // An empty block must still continue the multi-line docker run command.
        QString continuationBlock(const QStringList &arguments)
        {
            return arguments.isEmpty() ? QStringLiteral("    \\") : arguments.join(QLatin1Char('\n'));
        }

        QString continuedArguments(const QString &option, const QStringList &values)
        {
            QStringList arguments;
            for (const QString &value : values) {
                arguments.append(QStringLiteral("    %1 %2 \\").arg(option, shellQuote(value)));
            }
            return continuationBlock(arguments);
        }

        QString environmentArguments(const QMap<QString, QString> &environment)
        {
            QStringList arguments;
            for (auto it = environment.cbegin(); it != environment.cend(); ++it) {
                arguments.append(
                        QStringLiteral("    --env %1 \\").arg(shellQuote(it.key() + QLatin1Char('=') + it.value())));
            }
            return continuationBlock(arguments);
        }

        QString labelArguments(const QMap<QString, QString> &labels)
        {
            QStringList arguments;
            for (auto it = labels.cbegin(); it != labels.cend(); ++it) {
                arguments.append(
                        QStringLiteral("    --label %1 \\").arg(shellQuote(it.key() + QLatin1Char('=') + it.value())));
            }
            return continuationBlock(arguments);
        }

        QString volumeArguments(const QList<AgentWorkloadVolume> &volumes)
        {
            QStringList arguments;
            for (const auto &volume : volumes) {
                arguments.append(QStringLiteral("    --volume %1 \\")
                                         .arg(shellQuote(volume.name + QLatin1Char(':') + volume.target)));
            }
            return continuationBlock(arguments);
        }

    } // namespace

    std::optional<QString> renderAgentWorkloadApplyScript(const AgentWorkloadDeploymentSpec &spec,
                                                          AgentWorkloadReconciliationAction action,
                                                          AgentWorkloadApplyRenderError *error)
    {
        if (validateAgentWorkloadDeploymentSpec(spec) != AgentDeploymentValidationError::None) {
            setError(error, AgentWorkloadApplyRenderError::InvalidDesiredState);
            return std::nullopt;
        }

        const QString renderedAction = actionName(action);
        if (renderedAction.isEmpty()) {
            setError(error, AgentWorkloadApplyRenderError::UnsupportedAction);
            return std::nullopt;
        }

        const DockerContainer container = spec.workload == QStringLiteral("amgpt-device-gateway")
                ? DockerContainer::AmgptAuthProxy
                : DockerContainer::OpenClawCodex;
        QString script = scriptData(ProtocolScriptType::run_container, container);
        if (script.isEmpty()) {
            setError(error, AgentWorkloadApplyRenderError::MissingPlaybook);
            return std::nullopt;
        }
        script.replace(QStringLiteral("\r"), QString());

        QStringList volumeNames;
        for (const auto &volume : spec.volumes) {
            volumeNames.append(shellQuote(volume.name));
        }
        const int healthAttempts = std::max(
                1, spec.healthCheck.startPeriodSeconds + spec.healthCheck.intervalSeconds * spec.healthCheck.retries);

        const QMap<QString, QString> replacements {
            { QStringLiteral("@@ACTION@@"), shellQuote(renderedAction) },
            { QStringLiteral("@@CONTAINER_NAME@@"), shellQuote(spec.containerName) },
            { QStringLiteral("@@ENV_ARGS@@"), environmentArguments(spec.environment) },
            { QStringLiteral("@@HEALTH_ATTEMPTS@@"), QString::number(healthAttempts) },
            { QStringLiteral("@@HEALTH_INTERVAL@@"),
              shellQuote(QString::number(spec.healthCheck.intervalSeconds) + QStringLiteral("s")) },
            { QStringLiteral("@@HEALTH_RETRIES@@"), shellQuote(QString::number(spec.healthCheck.retries)) },
            { QStringLiteral("@@HEALTH_START_PERIOD@@"),
              shellQuote(QString::number(spec.healthCheck.startPeriodSeconds) + QStringLiteral("s")) },
            { QStringLiteral("@@HEALTH_TIMEOUT@@"),
              shellQuote(QString::number(spec.healthCheck.timeoutSeconds) + QStringLiteral("s")) },
            { QStringLiteral("@@IMAGE@@"), shellQuote(spec.imageReference) },
            { QStringLiteral("@@LABEL_ARGS@@"), labelArguments(spec.deploymentLabels()) },
            { QStringLiteral("@@MANAGED_BY@@"),
              shellQuote(spec.deploymentLabels().value(QStringLiteral("org.amnezia.amgpt.deployment.managed-by"))) },
            { QStringLiteral("@@NETWORK_ALIAS_ARG@@"),
              spec.networkAlias.isEmpty()
                      ? QStringLiteral("    \\")
                      : QStringLiteral("    --network-alias %1 \\").arg(shellQuote(spec.networkAlias)) },
            { QStringLiteral("@@NETWORK_NAME@@"), shellQuote(spec.networkName) },
            { QStringLiteral("@@PLATFORM@@"), shellQuote(spec.platform) },
            { QStringLiteral("@@PUBLISH_ARG@@"),
              spec.hostPort.isEmpty()
                      ? QStringLiteral("    \\")
                      : QStringLiteral("    --publish %1 \\").arg(shellQuote(
                                spec.hostPort + QLatin1Char(':') + spec.containerPort + QStringLiteral("/tcp"))) },
            { QStringLiteral("@@RESTART_POLICY@@"), shellQuote(spec.restartPolicy) },
            { QStringLiteral("@@SECURITY_ARGS@@"),
              continuedArguments(QStringLiteral("--security-opt"), spec.securityOptions) },
            { QStringLiteral("@@CAPABILITY_ARGS@@"),
              continuedArguments(QStringLiteral("--cap-drop"), spec.capabilitiesDropped)
                      + (spec.capabilitiesAdded.isEmpty()
                                 ? QString()
                                 : QLatin1Char('\n')
                                         + continuedArguments(QStringLiteral("--cap-add"), spec.capabilitiesAdded)) },
            { QStringLiteral("@@STOP_TIMEOUT@@"), shellQuote(QString::number(spec.stopGracePeriodSeconds)) },
            { QStringLiteral("@@TMPFS_ARGS@@"), continuedArguments(QStringLiteral("--tmpfs"), spec.tmpfs) },
            { QStringLiteral("@@VOLUME_ARGS@@"), volumeArguments(spec.volumes) },
            { QStringLiteral("@@VOLUME_NAMES@@"), volumeNames.join(QLatin1Char(' ')) },
            { QStringLiteral("@@WORKLOAD@@"), shellQuote(spec.workload) },
        };

        for (auto it = replacements.cbegin(); it != replacements.cend(); ++it) {
            script.replace(it.key(), it.value());
        }
        if (script.contains(QRegularExpression(QStringLiteral("@@[A-Z_]+@@")))) {
            setError(error, AgentWorkloadApplyRenderError::UnresolvedPlaceholder);
            return std::nullopt;
        }

        setError(error, AgentWorkloadApplyRenderError::None);
        return script;
    }

    QString agentWorkloadApplyCommand(const QString &renderedScript)
    {
        const QByteArray payload = renderedScript.toUtf8().toBase64();
        return QStringLiteral("payload='%1'; printf '%s' \"$payload\" | base64 -d | sudo -n sh")
                .arg(QString::fromLatin1(payload));
    }

} // namespace amnezia
