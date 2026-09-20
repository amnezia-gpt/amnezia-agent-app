#include "agentWorkloadDeploymentSpec.h"

#include "core/models/protocols/amgptAuthProxyProtocolConfig.h"
#include "core/models/protocols/openClawCodexProtocolConfig.h"
#include "core/utils/constants/protocolConstants.h"

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
        constexpr auto WorkloadVersion = "v0.2.0";
        constexpr auto Platform = "linux/amd64";
        constexpr auto NetworkName = "amnezia-agent-workloads";
        constexpr auto ManagedBy = "amnezia-agent-app";
        constexpr auto RestartPolicy = "unless-stopped";
        constexpr auto AuthProxyImage = "docker.io/amneziavpn/agent-workload-amgpt-auth-proxy:v0.2.1";
        constexpr auto OpenClawImage = "docker.io/amneziavpn/agent-workload-openclaw-codex:v0.2.0";
        constexpr auto LabelNamespace = "org.amnezia.amgpt.deployment.";

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
            if (profile.authIssuer.isEmpty() || profile.routerBaseUrl.isEmpty()) {
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

            setError(error, AgentDeploymentValidationError::None);
            return AgentBackendProfile { profile.id, *issuer, *router };
        }

        std::optional<QString> normalizedPort(const QString &configured, const char *defaultPort,
                                              AgentDeploymentValidationError *error)
        {
            const QString value = configured.isEmpty() ? QString::fromLatin1(defaultPort) : configured;
            bool converted = false;
            const int port = value.toInt(&converted);
            if (!converted || port < 1 || port > 65535 || value != QString::number(port)) {
                setError(error, AgentDeploymentValidationError::InvalidPort);
                return std::nullopt;
            }
            return QString::number(port);
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
                                            { QStringLiteral("router_base_url"), spec.backendProfile->routerBaseUrl } });
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
            variables.insert(QStringLiteral("$AMGPT_AUTH_PROXY_PORT"), hostPort);
        } else {
            variables.insert(QStringLiteral("$OPENCLAW_CODEX_PORT"), hostPort);
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
        const auto profile = normalizedProfile({ config.backendProfile, config.authIssuer, config.routerBaseUrl }, error);
        if (!profile) {
            return std::nullopt;
        }
        const auto port = normalizedPort(config.port, protocols::amgptAuthProxy::defaultPort, error);
        if (!port) {
            return std::nullopt;
        }

        AgentWorkloadDeploymentSpec spec = commonSpec();
        spec.workload = QStringLiteral("amgpt-auth-proxy");
        spec.workloadVersion = QStringLiteral("v0.2.1");
        spec.imageReference = QString::fromLatin1(AuthProxyImage);
        spec.containerName = QStringLiteral("amnezia-amgpt-auth-proxy");
        spec.networkAlias = QStringLiteral("amgpt-auth-proxy");
        spec.hostPort = *port;
        spec.containerPort = QString::fromLatin1(protocols::amgptAuthProxy::defaultPort);
        spec.environment = {
            { QStringLiteral("AMGPT_AUTH_ISSUER"), profile->authIssuer },
            { QStringLiteral("AMGPT_PROXY_STATE_DIR"), QStringLiteral("/var/lib/amgpt-auth-proxy") },
            { QStringLiteral("AMGPT_ROUTER_BASE_URL"), profile->routerBaseUrl },
        };
        spec.volumes = {
            { QStringLiteral("amnezia-amgpt-auth-proxy-state"), QStringLiteral("/var/lib/amgpt-auth-proxy") },
        };
        spec.tmpfs = { QStringLiteral("/tmp:rw,noexec,nosuid,nodev") };
        spec.healthCheck = {
            { QStringLiteral("/usr/local/bin/amgpt-auth-proxy"), QStringLiteral("healthcheck") }, 10, 5, 3, 5
        };
        spec.stopGracePeriodSeconds = 15;
        spec.backendProfile = *profile;

        setError(error, AgentDeploymentValidationError::None);
        return spec;
    }

    std::optional<AgentWorkloadDeploymentSpec> makeAgentWorkloadDeploymentSpec(const OpenClawCodexProtocolConfig &config,
                                                                               AgentDeploymentValidationError *error)
    {
        const auto port = normalizedPort(config.port, protocols::openClawCodex::defaultPort, error);
        if (!port) {
            return std::nullopt;
        }

        AgentWorkloadDeploymentSpec spec = commonSpec();
        spec.workload = QStringLiteral("openclaw-codex");
        spec.imageReference = QString::fromLatin1(OpenClawImage);
        spec.containerName = QStringLiteral("amnezia-openclaw-codex");
        spec.hostPort = *port;
        spec.containerPort = QString::fromLatin1(protocols::openClawCodex::defaultPort);
        spec.environment = {
            { QStringLiteral("AMGPT_AUTH_PROXY_URL"), QStringLiteral("http://amgpt-auth-proxy:8080") },
            { QStringLiteral("CODEX_HOME"), QStringLiteral("/home/node/.codex") },
            { QStringLiteral("HOME"), QStringLiteral("/home/node") },
            { QStringLiteral("OPENCLAW_CONFIG_PATH"), QStringLiteral("/home/node/.openclaw/openclaw.json") },
            { QStringLiteral("OPENCLAW_STATE_DIR"), QStringLiteral("/home/node/.openclaw") },
            { QStringLiteral("OPENCLAW_WORKSPACE"), QStringLiteral("/workspace") },
            { QStringLiteral("WORKLOAD_LOGIN_MARKER"), QStringLiteral("/run/workload/login.json") },
        };
        spec.volumes = {
            { QStringLiteral("amnezia-openclaw-state"), QStringLiteral("/home/node/.openclaw") },
            { QStringLiteral("amnezia-codex-home"), QStringLiteral("/home/node/.codex") },
            { QStringLiteral("amnezia-openclaw-workspace"), QStringLiteral("/workspace") },
        };
        spec.tmpfs = {
            QStringLiteral("/run/workload:rw,noexec,nosuid,nodev,uid=1000,gid=1000,mode=0700"),
            QStringLiteral("/tmp:rw,noexec,nosuid,nodev"),
        };
        spec.healthCheck = {
            { QStringLiteral("node"), QStringLiteral("-e"),
              QStringLiteral("fetch('http://127.0.0.1:18789/healthz').then((r) => process.exit(r.ok ? 0 : 1)).catch(() "
                             "=> process.exit(1))") },
            10,
            5,
            3,
            10,
        };
        spec.stopGracePeriodSeconds = 20;

        setError(error, AgentDeploymentValidationError::None);
        return spec;
    }

    AgentDeploymentValidationError validateAgentWorkloadDeploymentSpec(const AgentWorkloadDeploymentSpec &spec)
    {
        AgentDeploymentValidationError error = AgentDeploymentValidationError::None;
        std::optional<AgentWorkloadDeploymentSpec> expected;

        if (spec.workload == QStringLiteral("amgpt-auth-proxy")) {
            if (!spec.backendProfile) {
                return AgentDeploymentValidationError::MissingBackendProfile;
            }

            AmgptAuthProxyProtocolConfig config;
            config.port = spec.hostPort;
            config.backendProfile = spec.backendProfile->id;
            config.authIssuer = spec.backendProfile->authIssuer;
            config.routerBaseUrl = spec.backendProfile->routerBaseUrl;
            expected = makeAgentWorkloadDeploymentSpec(config, &error);
        } else if (spec.workload == QStringLiteral("openclaw-codex")) {
            if (spec.backendProfile) {
                return AgentDeploymentValidationError::InvalidDesiredState;
            }

            OpenClawCodexProtocolConfig config;
            config.port = spec.hostPort;
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
