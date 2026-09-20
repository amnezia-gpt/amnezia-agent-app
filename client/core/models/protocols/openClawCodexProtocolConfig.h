#ifndef OPENCLAWCODEXPROTOCOLCONFIG_H
#define OPENCLAWCODEXPROTOCOLCONFIG_H

#include <QJsonObject>
#include <QString>

namespace amnezia
{

    struct OpenClawCodexProtocolConfig
    {
        QString port;

        QJsonObject toJson() const;
        static OpenClawCodexProtocolConfig fromJson(const QJsonObject &json);
    };

} // namespace amnezia

#endif // OPENCLAWCODEXPROTOCOLCONFIG_H
