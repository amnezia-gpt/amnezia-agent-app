#ifndef AGENTWORKLOADDEPLOYMENTSPEC_H
#define AGENTWORKLOADDEPLOYMENTSPEC_H

#include <QMap>
#include <QMetaType>
#include <QString>
#include <QStringList>

#include <optional>

namespace amnezia
{

    struct AmgptAuthProxyProtocolConfig;
    struct OpenClawCodexProtocolConfig;

    enum class AgentBackendEnvironment {
        Production,
        Development,
        Local,
    };

    enum class AgentDeploymentValidationError {
        None,
        UnsupportedEnvironment,
        MissingBackendProfile,
        InvalidBackendProfileId,
        InvalidAuthIssuer,
        InvalidRouterBaseUrl,
        UnsupportedWorkload,
        InvalidDesiredState,
    };

    struct AgentBackendProfile
    {
        QString id;
        QString authIssuer;
        QString routerBaseUrl;
    };

    struct AgentBackendProfileCatalog
    {
        AgentBackendProfile production;
        AgentBackendProfile development;
        AgentBackendProfile local;
    };

    struct AgentWorkloadVolume
    {
        QString name;
        QString target;
    };

    struct AgentWorkloadHealthCheck
    {
        QStringList command;
        int intervalSeconds = 0;
        int timeoutSeconds = 0;
        int retries = 0;
        int startPeriodSeconds = 0;
    };

    struct AgentWorkloadDeploymentSpec
    {
        int schemaVersion = 0;
        QString workload;
        QString workloadVersion;
        QString imageReference;
        QString platform;
        QString containerName;
        QString networkName;
        QString networkAlias;
        // Empty when the workload publishes no host port.
        QString hostPort;
        QString containerPort;
        QString restartPolicy;
        QMap<QString, QString> environment;
        QList<AgentWorkloadVolume> volumes;
        QStringList tmpfs;
        QStringList capabilitiesDropped;
        QStringList capabilitiesAdded;
        QStringList securityOptions;
        AgentWorkloadHealthCheck healthCheck;
        int stopGracePeriodSeconds = 0;
        std::optional<AgentBackendProfile> backendProfile;

        QString specHash() const;
        QMap<QString, QString> deploymentLabels() const;
        QMap<QString, QString> templateVariables() const;
    };

    std::optional<AgentBackendProfile> resolveAgentBackendProfile(AgentBackendEnvironment environment,
                                                                  const AgentBackendProfileCatalog &catalog,
                                                                  AgentDeploymentValidationError *error = nullptr);

    std::optional<AgentWorkloadDeploymentSpec>
    makeAgentWorkloadDeploymentSpec(const AmgptAuthProxyProtocolConfig &config,
                                    AgentDeploymentValidationError *error = nullptr);

    std::optional<AgentWorkloadDeploymentSpec>
    makeAgentWorkloadDeploymentSpec(const OpenClawCodexProtocolConfig &config,
                                    AgentDeploymentValidationError *error = nullptr);

    AgentDeploymentValidationError
    validateAgentWorkloadDeploymentSpec(const AgentWorkloadDeploymentSpec &spec);

} // namespace amnezia

Q_DECLARE_METATYPE(amnezia::AgentDeploymentValidationError)

#endif // AGENTWORKLOADDEPLOYMENTSPEC_H
