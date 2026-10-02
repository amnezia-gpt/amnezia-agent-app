#include "agentWorkloadDeploymentSpec.h"

#include "core/models/protocols/amgptAuthProxyProtocolConfig.h"
#include "core/models/protocols/openClawCodexProtocolConfig.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>
#include <tuple>

namespace amnezia
{
    namespace
    {
        constexpr auto DeploymentSchemaVersion = 1;
        // Development deployments follow the moving agent-workloads `dev` channel for both images.
        constexpr auto WorkloadVersion = "dev";
        constexpr auto Platform = "linux/amd64";
        constexpr auto NetworkName = "amnezia-agent-workloads";
        constexpr auto ManagedBy = "amnezia-agent-app";
        constexpr auto RestartPolicy = "unless-stopped";
        constexpr auto DeviceGatewayImage = "docker.io/amneziavpn/agent-workload-amgpt-auth-proxy:dev";
        constexpr auto OpenClawImage = "docker.io/amneziavpn/agent-workload-openclaw-codex:dev";
        constexpr auto LabelNamespace = "org.amnezia.amgpt.deployment.";
        // The only resource the Device Gateway and the runtime share besides the network.
        constexpr auto CodexAppSocketVolume = "amnezia-agent-codex-app-socket";
        constexpr auto CodexAppSocketDirectory = "/run/amgpt-codex";

        void setError(AgentDeploymentValidationError *target, AgentDeploymentValidationError error)
        {
            if (target) {
                *target = error;
            }
        }

        bool isValidProfileId(const QString &value)
        {
            static const QRegularExpression pattern(QStringLiteral("^[a-z][a-z0-9._-]{0,63}$"));
            return pattern.match(value).hasMatch();
        }

        std::optional<QString> normalizedHttpsUrl(const QString &raw, bool requireV1Path,
                                                  AgentDeploymentValidationError urlError,
                                                  AgentDeploymentValidationError *error)
        {
            if (raw.isEmpty() || raw != raw.trimmed()) {
                setError(error, urlError);
                return std::nullopt;
            }

            QUrl url = QUrl::fromEncoded(raw.toUtf8(), QUrl::StrictMode);
            if (!url.isValid() || url.isRelative()
                || url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0 || url.host().isEmpty()
                || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()) {
                setError(error, urlError);
                return std::nullopt;
            }

            QString path = url.path(QUrl::FullyEncoded);
            if (requireV1Path && path != QStringLiteral("/v1") && path != QStringLiteral("/v1/")) {
                setError(error, urlError);
                return std::nullopt;
            }

            if (path.endsWith(QLatin1Char('/'))) {
                path.chop(1);
            }
            url.setScheme(QStringLiteral("https"));
            url.setHost(url.host().toLower());
            url.setPath(path, QUrl::StrictMode);

            setError(error, AgentDeploymentValidationError::None);
            return url.toString(QUrl::FullyEncoded);
        }

        std::optional<AgentBackendProfile> normalizedProfile(const AgentBackendProfile &profile,
                                                             AgentDeploymentValidationError *error)
        {
            if (profile.authIssuer.isEmpty() || profile.routerBaseUrl.isEmpty() || profile.runtimeGatewayBaseUrl.isEmpty()) {
                setError(error, AgentDeploymentValidationError::MissingBackendProfile);
                return std::nullopt;
            }
            if (!isValidProfileId(profile.id)) {
                setError(error, AgentDeploymentValidationError::InvalidBackendProfileId);
                return std::nullopt;
            }

            const auto issuer = normalizedHttpsUrl(profile.authIssuer, false,
                                                   AgentDeploymentValidationError::InvalidAuthIssuer, error);
            if (!issuer) {
                return std::nullopt;
            }
            const auto router = normalizedHttpsUrl(profile.routerBaseUrl, true,
                                                   AgentDeploymentValidationError::InvalidRouterBaseUrl, error);
            if (!router) {
                return std::nullopt;
            }

            const auto runtime = normalizedHttpsUrl(profile.runtimeGatewayBaseUrl, false,
                                                    AgentDeploymentValidationError::InvalidRuntimeGatewayBaseUrl, error);
            if (!runtime) {
                return std::nullopt;
            }
            QString runtimePath = QUrl(*runtime).path();
            while (runtimePath.endsWith(QLatin1Char('/'))) {
                runtimePath.chop(1);
            }
            if (runtimePath.endsWith(QStringLiteral("/v1"))) {
                setError(error, AgentDeploymentValidationError::InvalidRuntimeGatewayBaseUrl);
                return std::nullopt;
            }

            setError(error, AgentDeploymentValidationError::None);
            return AgentBackendProfile { profile.id, *issuer, *router, *runtime };
        }

        QByteArray quotedJsonString(const QString &value)
        {
            const QByteArray wrapped = QJsonDocument(QJsonArray { value }).toJson(QJsonDocument::Compact);
            return wrapped.mid(1, wrapped.size() - 2);
        }

        QByteArray canonicalJson(const QJsonValue &value)
        {
            if (value.isObject()) {
                const QJsonObject object = value.toObject();
                QStringList keys = object.keys();
                std::sort(keys.begin(), keys.end());

                QByteArray result("{");
                for (const QString &key : keys) {
                    if (result.size() > 1) {
                        result.append(',');
                    }
                    result.append(quotedJsonString(key));
                    result.append(':');
                    result.append(canonicalJson(object.value(key)));
                }
                result.append('}');
                return result;
            }
            if (value.isArray()) {
                const QJsonArray array = value.toArray();
                QByteArray result("[");
                for (qsizetype index = 0; index < array.size(); ++index) {
                    if (index > 0) {
                        result.append(',');
                    }
                    result.append(canonicalJson(array.at(index)));
                }
                result.append(']');
                return result;
            }

            const QByteArray wrapped = QJsonDocument(QJsonArray { value }).toJson(QJsonDocument::Compact);
            return wrapped.mid(1, wrapped.size() - 2);
        }

        QJsonArray sortedStrings(QStringList values)
        {
            std::sort(values.begin(), values.end());
            return QJsonArray::fromStringList(values);
        }

        QJsonObject canonicalSpecObject(const AgentWorkloadDeploymentSpec &spec)
        {
            QJsonObject environment;
            for (auto it = spec.environment.cbegin(); it != spec.environment.cend(); ++it) {
                environment.insert(it.key(), it.value());
            }

            QList<AgentWorkloadVolume> volumes = spec.volumes;
            std::sort(volumes.begin(), volumes.end(), [](const auto &left, const auto &right) {
                return std::tie(left.name, left.target) < std::tie(right.name, right.target);
            });
            QJsonArray volumeArray;
            for (const AgentWorkloadVolume &volume : volumes) {
                volumeArray.append(QJsonObject { { QStringLiteral("name"), volume.name },
                                                 { QStringLiteral("target"), volume.target } });
            }

            QJsonObject healthCheck {
                { QStringLiteral("command"), QJsonArray::fromStringList(spec.healthCheck.command) },
                { QStringLiteral("interval_seconds"), spec.healthCheck.intervalSeconds },
                { QStringLiteral("retries"), spec.healthCheck.retries },
                { QStringLiteral("start_period_seconds"), spec.healthCheck.startPeriodSeconds },
                { QStringLiteral("timeout_seconds"), spec.healthCheck.timeoutSeconds },
            };

            QJsonObject object {
                { QStringLiteral("capabilities_added"), sortedStrings(spec.capabilitiesAdded) },
                { QStringLiteral("capabilities_dropped"), sortedStrings(spec.capabilitiesDropped) },
                { QStringLiteral("container_name"), spec.containerName },
                { QStringLiteral("container_port"), spec.containerPort },
                { QStringLiteral("environment"), environment },
                { QStringLiteral("health_check"), healthCheck },
                { QStringLiteral("host_port"), spec.hostPort },
                { QStringLiteral("image_reference"), spec.imageReference },
                { QStringLiteral("network_alias"), spec.networkAlias },
                { QStringLiteral("network_name"), spec.networkName },
                { QStringLiteral("platform"), spec.platform },
                { QStringLiteral("restart_policy"), spec.restartPolicy },
                { QStringLiteral("schema_version"), spec.schemaVersion },
                { QStringLiteral("security_options"), sortedStrings(spec.securityOptions) },
                { QStringLiteral("stop_grace_period_seconds"), spec.stopGracePeriodSeconds },
                { QStringLiteral("tmpfs"), sortedStrings(spec.tmpfs) },
                { QStringLiteral("volumes"), volumeArray },
                { QStringLiteral("workload"), spec.workload },
                { QStringLiteral("workload_version"), spec.workloadVersion },
            };
            if (spec.backendProfile) {
                object.insert(QStringLiteral("backend_profile"),
                              QJsonObject { { QStringLiteral("auth_issuer"), spec.backendProfile->authIssuer },
                                            { QStringLiteral("id"), spec.backendProfile->id },
                                            { QStringLiteral("router_base_url"), spec.backendProfile->routerBaseUrl },
                                            { QStringLiteral("runtime_gateway_base_url"),
                                              spec.backendProfile->runtimeGatewayBaseUrl } });
            }
            return object;
        }

        AgentWorkloadDeploymentSpec commonSpec()
        {
            AgentWorkloadDeploymentSpec spec;
            spec.schemaVersion = DeploymentSchemaVersion;
            spec.workloadVersion = QString::fromLatin1(WorkloadVersion);
            spec.platform = QString::fromLatin1(Platform);
            spec.networkName = QString::fromLatin1(NetworkName);
            spec.restartPolicy = QString::fromLatin1(RestartPolicy);
            spec.capabilitiesDropped = { QStringLiteral("ALL") };
            spec.securityOptions = { QStringLiteral("no-new-privileges") };
            return spec;
        }
    } // namespace

    QString AgentWorkloadDeploymentSpec::specHash() const
    {
        return QString::fromLatin1(
                QCryptographicHash::hash(canonicalJson(canonicalSpecObject(*this)), QCryptographicHash::Sha256).toHex());
    }

    QMap<QString, QString> AgentWorkloadDeploymentSpec::deploymentLabels() const
    {
        QMap<QString, QString> labels {
            { QString::fromLatin1(LabelNamespace) + QStringLiteral("managed-by"), QString::fromLatin1(ManagedBy) },
            { QString::fromLatin1(LabelNamespace) + QStringLiteral("schema-version"), QString::number(schemaVersion) },
            { QString::fromLatin1(LabelNamespace) + QStringLiteral("spec-hash"), specHash() },
            { QString::fromLatin1(LabelNamespace) + QStringLiteral("workload"), workload },
            { QString::fromLatin1(LabelNamespace) + QStringLiteral("workload-version"), workloadVersion },
        };
        if (backendProfile) {
            labels.insert(QString::fromLatin1(LabelNamespace) + QStringLiteral("backend-profile"), backendProfile->id);
        }
        return labels;
    }

    QMap<QString, QString> AgentWorkloadDeploymentSpec::templateVariables() const
    {
        QMap<QString, QString> variables {
            { QStringLiteral("$AGENT_CONTAINER_NAME"), containerName },
            { QStringLiteral("$AGENT_DEPLOYMENT_SCHEMA_VERSION"), QString::number(schemaVersion) },
            { QStringLiteral("$AGENT_DEPLOYMENT_SPEC_HASH"), specHash() },
            { QStringLiteral("$AGENT_MANAGED_BY"), QString::fromLatin1(ManagedBy) },
            { QStringLiteral("$AGENT_WORKLOAD_ID"), workload },
            { QStringLiteral("$AGENT_WORKLOAD_IMAGE"), imageReference },
            { QStringLiteral("$AGENT_WORKLOAD_NETWORK"), networkName },
            { QStringLiteral("$AGENT_WORKLOAD_PLATFORM"), platform },
            { QStringLiteral("$AGENT_WORKLOAD_VERSION"), workloadVersion },
        };

        if (backendProfile) {
            variables.insert(QStringLiteral("$AGENT_BACKEND_PROFILE"), backendProfile->id);
            variables.insert(QStringLiteral("$AMGPT_AUTH_ISSUER"), backendProfile->authIssuer);
            variables.insert(QStringLiteral("$AMGPT_ROUTER_BASE_URL"), backendProfile->routerBaseUrl);
            variables.insert(QStringLiteral("$AMGPT_RUNTIME_GATEWAY_BASE_URL"), backendProfile->runtimeGatewayBaseUrl);
        }
        return variables;
    }

    std::optional<AgentBackendProfile> resolveAgentBackendProfile(AgentBackendEnvironment environment,
                                                                  const AgentBackendProfileCatalog &catalog,
                                                                  AgentDeploymentValidationError *error)
    {
        switch (environment) {
        case AgentBackendEnvironment::Production: return normalizedProfile(catalog.production, error);
        case AgentBackendEnvironment::Development: return normalizedProfile(catalog.development, error);
        case AgentBackendEnvironment::Local: return normalizedProfile(catalog.local, error);
        }
        setError(error, AgentDeploymentValidationError::UnsupportedEnvironment);
        return std::nullopt;
    }

    std::optional<AgentWorkloadDeploymentSpec> makeAgentWorkloadDeploymentSpec(const AmgptAuthProxyProtocolConfig &config,
                                                                               AgentDeploymentValidationError *error)
    {
        const auto profile = normalizedProfile(
                { config.backendProfile, config.authIssuer, config.routerBaseUrl, config.runtimeGatewayBaseUrl }, error);
        if (!profile) {
            return std::nullopt;
        }
        // Persisted ports from earlier releases are ignored: the private /v1 endpoint injects Router
        // credentials, so it must stay reachable only on the workload network.
        AgentWorkloadDeploymentSpec spec = commonSpec();
        spec.workload = QStringLiteral("amgpt-device-gateway");
        spec.imageReference = QString::fromLatin1(DeviceGatewayImage);
        spec.containerName = QStringLiteral("amnezia-amgpt-device-gateway");
        spec.networkAlias = QStringLiteral("amgpt-device-gateway");
        spec.environment = {
            { QStringLiteral("AMGPT_AUTH_ISSUER"), profile->authIssuer },
            { QStringLiteral("AMGPT_CODEX_APP_SOCKET"),
              QString::fromLatin1(CodexAppSocketDirectory) + QStringLiteral("/app-server.sock") },
            { QStringLiteral("AMGPT_DEVICE_GATEWAY_STATE_DIR"), QStringLiteral("/var/lib/amgpt-device-gateway") },
            { QStringLiteral("AMGPT_INGRESS_STATE_DIR"), QStringLiteral("/var/lib/amgpt-device-gateway/ingress") },
            { QStringLiteral("AMGPT_ROUTER_BASE_URL"), profile->routerBaseUrl },
            { QStringLiteral("AMGPT_RUNTIME_GATEWAY_BASE_URL"), profile->runtimeGatewayBaseUrl },
        };
        spec.volumes = {
            { QStringLiteral("amnezia-amgpt-device-gateway-state"),
              QStringLiteral("/var/lib/amgpt-device-gateway") },
            { QString::fromLatin1(CodexAppSocketVolume), QString::fromLatin1(CodexAppSocketDirectory) },
        };
        spec.tmpfs = { QStringLiteral("/tmp:rw,noexec,nosuid,nodev") };
        spec.healthCheck = {
            { QStringLiteral("/usr/local/bin/amgpt-device-gateway"), QStringLiteral("healthcheck") }, 10, 5, 3, 5
        };
        spec.stopGracePeriodSeconds = 15;
        spec.backendProfile = *profile;

        setError(error, AgentDeploymentValidationError::None);
        return spec;
    }

    std::optional<AgentWorkloadDeploymentSpec> makeAgentWorkloadDeploymentSpec(const OpenClawCodexProtocolConfig &,
                                                                               AgentDeploymentValidationError *error)
    {
        // The supervised runtime publishes no port. The image owns HOME, CODEX_HOME and OpenClaw paths
        // for its separate service identities, and its user must not be overridden.
        AgentWorkloadDeploymentSpec spec = commonSpec();
        spec.workload = QStringLiteral("openclaw-codex");
        spec.imageReference = QString::fromLatin1(OpenClawImage);
        spec.containerName = QStringLiteral("amnezia-openclaw-codex");
        spec.volumes = {
            { QStringLiteral("amnezia-agent-runtime-openclaw-state"), QStringLiteral("/home/openclaw/.openclaw") },
            { QStringLiteral("amnezia-agent-runtime-workspace"), QStringLiteral("/workspace") },
            { QStringLiteral("amnezia-agent-runtime-codex-home"), QStringLiteral("/home/codex/.codex") },
            { QStringLiteral("amnezia-agent-runtime-codex-workspace"), QStringLiteral("/codex-workspace") },
            { QString::fromLatin1(CodexAppSocketVolume), QString::fromLatin1(CodexAppSocketDirectory) },
        };
        spec.tmpfs = { QStringLiteral("/tmp:rw,noexec,nosuid,nodev") };
        // The root supervisor needs only these to start services under their own identities and signal them.
        spec.capabilitiesAdded = { QStringLiteral("SETUID"), QStringLiteral("SETGID"), QStringLiteral("KILL") };
        spec.healthCheck = {
            { QStringLiteral("node"), QStringLiteral("/opt/workload/bin/runtimectl.mjs"),
              QStringLiteral("healthcheck") },
            10,
            5,
            3,
            60,
        };
        spec.stopGracePeriodSeconds = 20;

        setError(error, AgentDeploymentValidationError::None);
        return spec;
    }

    AgentDeploymentValidationError validateAgentWorkloadDeploymentSpec(const AgentWorkloadDeploymentSpec &spec)
    {
        AgentDeploymentValidationError error = AgentDeploymentValidationError::None;
        std::optional<AgentWorkloadDeploymentSpec> expected;

        if (spec.workload == QStringLiteral("amgpt-device-gateway")) {
            if (!spec.backendProfile) {
                return AgentDeploymentValidationError::MissingBackendProfile;
            }

            AmgptAuthProxyProtocolConfig config;
            config.backendProfile = spec.backendProfile->id;
            config.authIssuer = spec.backendProfile->authIssuer;
            config.routerBaseUrl = spec.backendProfile->routerBaseUrl;
            config.runtimeGatewayBaseUrl = spec.backendProfile->runtimeGatewayBaseUrl;
            expected = makeAgentWorkloadDeploymentSpec(config, &error);
        } else if (spec.workload == QStringLiteral("openclaw-codex")) {
            if (spec.backendProfile) {
                return AgentDeploymentValidationError::InvalidDesiredState;
            }

            OpenClawCodexProtocolConfig config;
            expected = makeAgentWorkloadDeploymentSpec(config, &error);
        } else {
            return AgentDeploymentValidationError::UnsupportedWorkload;
        }

        if (!expected) {
            return error;
        }
        return expected->specHash() == spec.specHash() ? AgentDeploymentValidationError::None
                                                       : AgentDeploymentValidationError::InvalidDesiredState;
    }

} // namespace amnezia
