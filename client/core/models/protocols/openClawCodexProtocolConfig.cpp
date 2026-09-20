#include "openClawCodexProtocolConfig.h"

#include "core/utils/constants/configKeys.h"

namespace amnezia
{

    QJsonObject OpenClawCodexProtocolConfig::toJson() const
    {
        QJsonObject object;
        if (!port.isEmpty()) {
            object[configKey::port] = port;
        }
        return object;
    }

    OpenClawCodexProtocolConfig OpenClawCodexProtocolConfig::fromJson(const QJsonObject &json)
    {
        OpenClawCodexProtocolConfig config;
        config.port = json.value(configKey::port).toString();
        return config;
    }

} // namespace amnezia
