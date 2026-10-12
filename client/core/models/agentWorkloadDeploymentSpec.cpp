#include "agentWorkloadDeploymentSpec.h"

#include "core/models/protocols/amgptAuthProxyProtocolConfig.h"
#include "core/models/protocols/openClawCodexProtocolConfig.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QSet>

#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <memory>

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

        // QJsonDocument accepts duplicate members. Reject them before consulting its object view.
        bool uniqueJsonMembers(const QByteArray &data)
        {
            QList<QSet<QString>> objects;
            for (qsizetype i = 0; i < data.size(); ++i) {
                if (data[i] == '{') objects.append(QSet<QString> {});
                else if (data[i] == '}') objects.removeLast();
                else if (data[i] == '"') {
                    const qsizetype start = i++;
                    while (i < data.size() && data[i] != '"') {
                        if (data[i] == '\\') ++i;
                        ++i;
                    }
                    qsizetype next = i + 1;
                    while (next < data.size() && QByteArray(" \r\n\t").contains(data[next])) ++next;
                    if (next < data.size() && data[next] == ':') {
                        const auto key = QJsonDocument::fromJson("[" + data.mid(start, i - start + 1) + "]")
                                                 .array().first().toString();
                        if (objects.isEmpty() || objects.last().contains(key)) return false;
                        if (objects.size() == 1 && key == QStringLiteral("schema_version")) {
                            qsizetype valueStart = next + 1;
                            while (valueStart < data.size() && QByteArray(" \r\n\t").contains(data[valueStart])) ++valueStart;
                            if (valueStart + 1 >= data.size() || data[valueStart] != '1'
                                || !QByteArray(" ,}\r\n\t").contains(data[valueStart + 1])) return false;
                        }
                        objects.last().insert(key);
                    }
                }
            }
            return objects.isEmpty();
        }

        bool publicP256Point(const QString &x, const QString &y)
        {
            auto coordinate = [](const QString &value) {
                const QByteArray encoded = value.toLatin1();
                const auto decoded = QByteArray::fromBase64(encoded, QByteArray::Base64UrlEncoding);
                return decoded.size() == 32
                        && decoded.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals) == encoded
                        ? decoded : QByteArray {};
            };
            const auto xb = coordinate(x), yb = coordinate(y);
            if (xb.isEmpty() || yb.isEmpty()) return false;
            std::unique_ptr<EC_GROUP, decltype(&EC_GROUP_free)> group(EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1), EC_GROUP_free);
            if (!group) return false;
            std::unique_ptr<EC_POINT, decltype(&EC_POINT_free)> point(EC_POINT_new(group.get()), EC_POINT_free);
            const QByteArray octets = QByteArray(1, '\x04') + xb + yb;
            return point && EC_POINT_oct2point(group.get(), point.get(),
                       reinterpret_cast<const unsigned char *>(octets.constData()), octets.size(), nullptr) == 1
                    && EC_POINT_is_on_curve(group.get(), point.get(), nullptr) == 1
                    && EC_POINT_is_at_infinity(group.get(), point.get()) == 0;
        }

        bool validPublicTrust(const QString &bundle, const QString &issuer)
        {
            if (bundle.size() > 16 * 1024) return false;
            const QByteArray bytes = bundle.toUtf8();
            if (bytes.isEmpty() || bytes.size() > 16 * 1024 || bytes.startsWith("\xef\xbb\xbf") || bundle.contains(QChar(0))
                || QString::fromUtf8(bytes) != bundle) return false;
            const auto doc = QJsonDocument::fromJson(bytes);
            if (!doc.isObject() || !uniqueJsonMembers(bytes)) return false;
            const auto object = doc.object();
            if (object.size() != 3 || object.value("schema_version") != QJsonValue(1)
                || object.value("issuer") != QJsonValue(issuer) || !object.value("keys").isArray()) return false;
            const auto keys = object.value("keys").toArray();
            if (keys.isEmpty() || keys.size() > 8) return false;
            QSet<QString> kids;
            for (const auto &entry : keys) {
                if (!entry.isObject()) return false;
                const auto key = entry.toObject();
                if (key.size() != 7 || key.value("kty") != QJsonValue("EC") || key.value("crv") != QJsonValue("P-256")
                    || key.value("use") != QJsonValue("sig") || key.value("alg") != QJsonValue("ES256")
                    || !key.value("kid").isString() || !key.value("x").isString() || !key.value("y").isString()) return false;
                const QString kid = key.value("kid").toString();
                if (kid.isEmpty() || kid.toUtf8().size() > 128 || kids.contains(kid)
                    || !publicP256Point(key.value("x").toString(), key.value("y").toString())) return false;
                kids.insert(kid);
            }
            return true;
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

            if (!validPublicTrust(profile.ingressTrustBundle, *issuer)) {
                setError(error, AgentDeploymentValidationError::InvalidTrustBundle);
                return std::nullopt;
            }
            const auto pin = profile.ingressTrustSha256;
            static const QRegularExpression hexPin(QStringLiteral("^[0-9a-fA-F]{64}$"));
            if (!hexPin.match(pin).hasMatch()
                || pin.toLower().toLatin1() != QCryptographicHash::hash(profile.ingressTrustBundle.toUtf8(), QCryptographicHash::Sha256).toHex()) {
                setError(error, AgentDeploymentValidationError::InvalidTrustPin);
                return std::nullopt;
            }
            setError(error, AgentDeploymentValidationError::None);
            return AgentBackendProfile { profile.id, *issuer, *router, *runtime, profile.ingressTrustBundle, pin.toLower() };
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
                return std::tie(left.name, left.target, left.readOnly) < std::tie(right.name, right.target, right.readOnly);
            });
            QJsonArray volumeArray;
            for (const AgentWorkloadVolume &volume : volumes) {
                volumeArray.append(QJsonObject { { QStringLiteral("name"), volume.name },
                                                 { QStringLiteral("target"), volume.target },
                                                 { QStringLiteral("read_only"), volume.readOnly } });
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
                { config.backendProfile, config.authIssuer, config.routerBaseUrl, config.runtimeGatewayBaseUrl,
                  config.ingressTrustBundle, config.ingressTrustSha256 }, error);
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
            { QStringLiteral("AMGPT_DEVICE_GATEWAY_STATE_DIR"), QStringLiteral("/var/lib/amgpt-device-gateway") },
            { QStringLiteral("AMGPT_INGRESS_TRUST_BUNDLE"), profile->ingressTrustBundle },
            { QStringLiteral("AMGPT_INGRESS_TRUST_SHA256"), profile->ingressTrustSha256 },
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
        // The native image owns its user, command and gateway-published digest reader (ADR-017).
        // Retain legacy deployment identities for saved configurations and guard_owner.
        AgentWorkloadDeploymentSpec spec = commonSpec();
        spec.workload = QStringLiteral("openclaw-codex");
        spec.imageReference = QString::fromLatin1(OpenClawImage);
        spec.containerName = QStringLiteral("amnezia-openclaw-codex");
        spec.networkAlias = QStringLiteral("codex-app-server");
        spec.volumes = {
            { QStringLiteral("amnezia-agent-runtime-codex-home"), QStringLiteral("/home/codex/.codex") },
            { QStringLiteral("amnezia-agent-runtime-codex-workspace"), QStringLiteral("/codex-workspace") },
            { QString::fromLatin1(CodexAppSocketVolume), QString::fromLatin1(CodexAppSocketDirectory), true },
        };
        spec.tmpfs = { QStringLiteral("/tmp:rw,noexec,nosuid,nodev") };
        spec.healthCheck = {
            { QStringLiteral("node"), QStringLiteral("-e"),
              QStringLiteral("fetch('http://127.0.0.1:4500/readyz').then((r) => process.exit(r.ok ? 0 : 1), () => process.exit(1))") },
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
            config.ingressTrustBundle = spec.backendProfile->ingressTrustBundle;
            config.ingressTrustSha256 = spec.backendProfile->ingressTrustSha256;
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
