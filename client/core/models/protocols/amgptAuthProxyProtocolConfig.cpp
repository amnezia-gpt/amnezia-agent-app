#include "amgptAuthProxyProtocolConfig.h"

#include "core/utils/constants/configKeys.h"

namespace amnezia
{

    QJsonObject AmgptAuthProxyProtocolConfig::toJson() const
    {
        QJsonObject object;
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
        return config;
    }

} // namespace amnezia
