#ifndef AMGPTAUTHPROXYPROTOCOLCONFIG_H
#define AMGPTAUTHPROXYPROTOCOLCONFIG_H

#include <QJsonObject>
#include <QString>

namespace amnezia
{

    struct AmgptAuthProxyProtocolConfig
    {
        QString port;
        QString backendProfile;
        QString authIssuer;
        QString routerBaseUrl;

        QJsonObject toJson() const;
        static AmgptAuthProxyProtocolConfig fromJson(const QJsonObject &json);
    };

} // namespace amnezia

#endif // AMGPTAUTHPROXYPROTOCOLCONFIG_H
