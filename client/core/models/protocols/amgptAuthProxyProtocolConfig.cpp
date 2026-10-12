#include "amgptAuthProxyProtocolConfig.h"

#include "core/utils/constants/configKeys.h"

namespace amnezia
{

    QJsonObject AmgptAuthProxyProtocolConfig::toJson() const
    {
        QJsonObject object { { QStringLiteral("schema_version"), 3 } };
        if (!port.isEmpty()) {
            object[configKey::port] = port;
        }
        if (!backendProfile.isEmpty()) {
            object[configKey::backendProfile] = backendProfile;
        }
        if (!authIssuer.isEmpty()) {
            object[configKey::authIssuer] = authIssuer;
        }
        if (!routerBaseUrl.isEmpty()) {
            object[configKey::routerBaseUrl] = routerBaseUrl;
        }
        if (!runtimeGatewayBaseUrl.isEmpty()) {
            object[configKey::runtimeGatewayBaseUrl] = runtimeGatewayBaseUrl;
        }
        object[configKey::ingressTrustBundle] = ingressTrustBundle;
        object[configKey::ingressTrustSha256] = ingressTrustSha256;
        return object;
    }

    AmgptAuthProxyProtocolConfig AmgptAuthProxyProtocolConfig::fromJson(const QJsonObject &json)
    {
        AmgptAuthProxyProtocolConfig config;
        config.port = json.value(configKey::port).toString();
        config.backendProfile = json.value(configKey::backendProfile).toString();
        config.authIssuer = json.value(configKey::authIssuer).toString();
        config.routerBaseUrl = json.value(configKey::routerBaseUrl).toString();
        config.runtimeGatewayBaseUrl = json.value(configKey::runtimeGatewayBaseUrl).toString();
        config.ingressTrustBundle = json.value(configKey::ingressTrustBundle).toString();
        config.ingressTrustSha256 = json.value(configKey::ingressTrustSha256).toString();
        if (json.contains(QStringLiteral("schema_version")) && json.value(QStringLiteral("schema_version")) != QJsonValue(3)) {
            config.ingressTrustBundle.clear(); // Unsupported imported profiles cannot reach SSH.
            config.ingressTrustSha256.clear();
        }
        return config;
    }

} // namespace amnezia
