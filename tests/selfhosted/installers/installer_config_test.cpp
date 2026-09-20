#include <QtTest/QtTest>

#include <QRegularExpression>
#include <QSet>

#include <memory>
#include <variant>

#include "core/installers/awgInstaller.h"
#include "core/installers/installerBase.h"
#include "core/installers/mtProxyInstaller.h"
#include "core/installers/openvpnInstaller.h"
#include "core/installers/sftpInstaller.h"
#include "core/installers/socks5Installer.h"
#include "core/installers/telemtInstaller.h"
#include "core/installers/torInstaller.h"
#include "core/installers/wireguardInstaller.h"
#include "core/installers/xrayInstaller.h"
#include "core/models/protocols/amgptAuthProxyProtocolConfig.h"
#include "core/models/protocols/awgProtocolConfig.h"
#include "core/models/protocols/mtProxyProtocolConfig.h"
#include "core/models/protocols/openClawCodexProtocolConfig.h"
#include "core/models/protocols/openVpnProtocolConfig.h"
#include "core/models/protocols/sftpProtocolConfig.h"
#include "core/models/protocols/socks5ProxyProtocolConfig.h"
#include "core/models/protocols/telemtProtocolConfig.h"
#include "core/models/protocols/torProtocolConfig.h"
#include "core/models/protocols/wireGuardProtocolConfig.h"
#include "core/models/protocols/xrayProtocolConfig.h"
#include "core/utils/constants/protocolConstants.h"
#include "core/utils/containerEnum.h"
#include "core/utils/containers/containerUtils.h"
#include "core/utils/protocolEnum.h"

using namespace amnezia;

namespace
{
    struct InstallerRow
    {
        const char *name;
        DockerContainer container;
        int port;
        TransportProto transport;
        Proto protocol;
        const char *expectedTransport;
    };

    std::unique_ptr<InstallerBase> installerFor(DockerContainer container)
    {
        switch (container) {
        case DockerContainer::Awg:
        case DockerContainer::Awg2: return std::make_unique<AwgInstaller>();
        case DockerContainer::WireGuard: return std::make_unique<WireguardInstaller>();
        case DockerContainer::OpenVpn: return std::make_unique<OpenVpnInstaller>();
        case DockerContainer::Xray:
        case DockerContainer::SSXray: return std::make_unique<XrayInstaller>();
        case DockerContainer::TorWebSite: return std::make_unique<TorInstaller>();
        case DockerContainer::Sftp: return std::make_unique<SftpInstaller>();
        case DockerContainer::Socks5Proxy: return std::make_unique<Socks5Installer>();
        case DockerContainer::MtProxy: return std::make_unique<MtProxyInstaller>();
        case DockerContainer::Telemt: return std::make_unique<TelemtInstaller>();
        default: return std::make_unique<InstallerBase>();
        }
    }

    QString protocolPort(const ContainerConfig &config)
    {
        return config.protocolConfig.port();
    }

    QString protocolTransport(const ContainerConfig &config)
    {
        return config.protocolConfig.transportProto();
    }

    void addInstallerRow(const InstallerRow &row)
    {
        QTest::newRow(row.name) << static_cast<int>(row.container) << row.port << static_cast<int>(row.transport)
                                << static_cast<int>(row.protocol) << QString::fromLatin1(row.expectedTransport);
    }
} // namespace

class InstallerConfigTest : public QObject
{
    Q_OBJECT

private slots:
    void generatedConfig_data();
    void generatedConfig();
    void defaultConfig_data();
    void defaultConfig();
    void fallbackContainers_data();
    void fallbackContainers();
    void awgGeneratedParameters_data();
    void awgGeneratedParameters();
    void generatedCredentials_data();
    void generatedCredentials();
};

void InstallerConfigTest::generatedConfig_data()
{
    QTest::addColumn<int>("container");
    QTest::addColumn<int>("port");
    QTest::addColumn<int>("transport");
    QTest::addColumn<int>("protocol");
    QTest::addColumn<QString>("expectedTransport");

    addInstallerRow({ "awg-port-min", DockerContainer::Awg, 1, TransportProto::Udp, Proto::Awg, "udp" });
    addInstallerRow({ "awg2-port-max", DockerContainer::Awg2, 65535, TransportProto::Udp, Proto::Awg, "udp" });
    addInstallerRow({ "wireguard-port-min", DockerContainer::WireGuard, 1, TransportProto::Udp, Proto::WireGuard, "udp" });
    addInstallerRow(
            { "wireguard-port-max", DockerContainer::WireGuard, 65535, TransportProto::Udp, Proto::WireGuard, "udp" });
    addInstallerRow({ "openvpn-port-min", DockerContainer::OpenVpn, 1, TransportProto::Tcp, Proto::OpenVpn, "tcp" });
    addInstallerRow({ "openvpn-port-max", DockerContainer::OpenVpn, 65535, TransportProto::Udp, Proto::OpenVpn, "udp" });
    addInstallerRow({ "xray-port-min", DockerContainer::Xray, 1, TransportProto::Tcp, Proto::Xray, "tcp" });
    addInstallerRow({ "xray-port-max", DockerContainer::Xray, 65535, TransportProto::Tcp, Proto::Xray, "tcp" });
    addInstallerRow({ "ssxray-port-max", DockerContainer::SSXray, 65535, TransportProto::TcpAndUdp, Proto::SSXray,
                      "tcpandudp" });
    addInstallerRow({ "tor-port-ignored", DockerContainer::TorWebSite, 443, TransportProto::Tcp, Proto::TorWebSite, "" });
    addInstallerRow({ "sftp-port-min", DockerContainer::Sftp, 1, TransportProto::Tcp, Proto::Sftp, "" });
    addInstallerRow({ "sftp-port-max", DockerContainer::Sftp, 65535, TransportProto::Tcp, Proto::Sftp, "" });
    addInstallerRow({ "socks5-port-min", DockerContainer::Socks5Proxy, 1, TransportProto::Tcp, Proto::Socks5Proxy, "" });
    addInstallerRow(
            { "socks5-port-max", DockerContainer::Socks5Proxy, 65535, TransportProto::Tcp, Proto::Socks5Proxy, "" });
    addInstallerRow({ "mtproxy-port-min", DockerContainer::MtProxy, 1, TransportProto::Tcp, Proto::MtProxy, "tcp" });
    addInstallerRow({ "mtproxy-port-max", DockerContainer::MtProxy, 65535, TransportProto::Tcp, Proto::MtProxy, "tcp" });
    addInstallerRow({ "telemt-port-min", DockerContainer::Telemt, 1, TransportProto::Tcp, Proto::Telemt, "tcp" });
    addInstallerRow({ "telemt-port-max", DockerContainer::Telemt, 65535, TransportProto::Tcp, Proto::Telemt, "tcp" });
    addInstallerRow({ "amgpt-auth-proxy-port-min", DockerContainer::AmgptAuthProxy, 1, TransportProto::Tcp,
                      Proto::AmgptAuthProxy, "tcp" });
    addInstallerRow({ "amgpt-auth-proxy-port-max", DockerContainer::AmgptAuthProxy, 65535, TransportProto::Tcp,
                      Proto::AmgptAuthProxy, "tcp" });
    addInstallerRow({ "openclaw-codex-port-min", DockerContainer::OpenClawCodex, 1, TransportProto::Tcp,
                      Proto::OpenClawCodex, "tcp" });
    addInstallerRow({ "openclaw-codex-port-max", DockerContainer::OpenClawCodex, 65535, TransportProto::Tcp,
                      Proto::OpenClawCodex, "tcp" });
    // Invalid enum values are accepted by the production generator and produce an empty transport string.
    addInstallerRow({ "openvpn-unsupported-transport", DockerContainer::OpenVpn, 1, static_cast<TransportProto>(99),
                      Proto::OpenVpn, "" });
}

void InstallerConfigTest::generatedConfig()
{
    QFETCH(int, container);
    QFETCH(int, port);
    QFETCH(int, transport);
    QFETCH(int, protocol);
    QFETCH(QString, expectedTransport);

    const auto dockerContainer = static_cast<DockerContainer>(container);
    const auto installer = installerFor(dockerContainer);
    const ContainerConfig config =
            installer->generateConfig(dockerContainer, port, static_cast<TransportProto>(transport));

    QCOMPARE(static_cast<int>(config.container), container);
    QCOMPARE(static_cast<int>(config.getProtocolType()), protocol);
    if (static_cast<Proto>(protocol) == Proto::TorWebSite)
        QVERIFY(protocolPort(config).isEmpty());
    else
        QCOMPARE(protocolPort(config), QString::number(port));

    QCOMPARE(protocolTransport(config), expectedTransport);

    switch (static_cast<Proto>(protocol)) {
    case Proto::WireGuard: {
        const auto *wireguard = std::get_if<WireGuardProtocolConfig>(&config.protocolConfig.data);
        QVERIFY(wireguard != nullptr);
        QCOMPARE(wireguard->serverConfig.subnetAddress, QString::fromLatin1(protocols::wireguard::defaultSubnetAddress));
        break;
    }
    case Proto::OpenVpn: {
        const auto *openvpn = std::get_if<OpenVpnProtocolConfig>(&config.protocolConfig.data);
        QVERIFY(openvpn != nullptr);
        QVERIFY(!openvpn->serverConfig.ncpDisable);
        QVERIFY(openvpn->serverConfig.tlsAuth);
        QVERIFY(openvpn->serverConfig.subnetAddress.isEmpty());
        QVERIFY(openvpn->serverConfig.cipher.isEmpty());
        QVERIFY(openvpn->serverConfig.hash.isEmpty());
        break;
    }
    case Proto::Xray:
    case Proto::SSXray: {
        const auto *xray = std::get_if<XrayProtocolConfig>(&config.protocolConfig.data);
        QVERIFY(xray != nullptr);
        QCOMPARE(xray->serverConfig.transport, QString::fromLatin1(protocols::xray::defaultTransport));
        QCOMPARE(xray->serverConfig.security, QString::fromLatin1(protocols::xray::defaultSecurity));
        QCOMPARE(xray->serverConfig.flow, QString::fromLatin1(protocols::xray::defaultFlow));
        QCOMPARE(xray->serverConfig.site, QString::fromLatin1(protocols::xray::defaultSite));
        QCOMPARE(xray->serverConfig.sni, QString::fromLatin1(protocols::xray::defaultSni));
        QCOMPARE(xray->serverConfig.fingerprint, QString::fromLatin1(protocols::xray::defaultFingerprint));
        QCOMPARE(xray->serverConfig.alpn, QString::fromLatin1(protocols::xray::defaultAlpn));
        break;
    }
    case Proto::MtProxy: {
        const auto *mtproxy = std::get_if<MtProxyProtocolConfig>(&config.protocolConfig.data);
        QVERIFY(mtproxy != nullptr);
        QVERIFY(mtproxy->isEnabled);
        QVERIFY(mtproxy->secret.isEmpty());
        QVERIFY(mtproxy->tag.isEmpty());
        QVERIFY(mtproxy->publicHost.isEmpty());
        QVERIFY(mtproxy->transportMode.isEmpty());
        QVERIFY(mtproxy->tlsDomain.isEmpty());
        QVERIFY(mtproxy->additionalSecrets.isEmpty());
        QVERIFY(mtproxy->workersMode.isEmpty());
        QVERIFY(mtproxy->workers.isEmpty());
        QVERIFY(!mtproxy->natEnabled);
        QVERIFY(mtproxy->natInternalIp.isEmpty());
        QVERIFY(mtproxy->natExternalIp.isEmpty());
        break;
    }
    case Proto::Telemt: {
        const auto *telemt = std::get_if<TelemtProtocolConfig>(&config.protocolConfig.data);
        QVERIFY(telemt != nullptr);
        QVERIFY(telemt->isEnabled);
        QVERIFY(telemt->maskEnabled);
        QVERIFY(!telemt->tlsEmulation);
        QVERIFY(telemt->useMiddleProxy);
        QVERIFY(telemt->secret.isEmpty());
        QVERIFY(telemt->tag.isEmpty());
        QVERIFY(telemt->publicHost.isEmpty());
        QVERIFY(telemt->transportMode.isEmpty());
        QVERIFY(telemt->tlsDomain.isEmpty());
        QVERIFY(telemt->userName.isEmpty());
        QVERIFY(telemt->additionalSecrets.isEmpty());
        QVERIFY(telemt->workersMode.isEmpty());
        QVERIFY(telemt->workers.isEmpty());
        QVERIFY(!telemt->natEnabled);
        QVERIFY(telemt->natInternalIp.isEmpty());
        QVERIFY(telemt->natExternalIp.isEmpty());
        break;
    }
    case Proto::AmgptAuthProxy: {
        const auto *proxy = std::get_if<AmgptAuthProxyProtocolConfig>(&config.protocolConfig.data);
        QVERIFY(proxy != nullptr);
        QCOMPARE(proxy->port, QString::number(port));
        break;
    }
    case Proto::OpenClawCodex: {
        const auto *workload = std::get_if<OpenClawCodexProtocolConfig>(&config.protocolConfig.data);
        QVERIFY(workload != nullptr);
        QCOMPARE(workload->port, QString::number(port));
        break;
    }
    case Proto::TorWebSite: {
        const auto *tor = std::get_if<TorProtocolConfig>(&config.protocolConfig.data);
        QVERIFY(tor != nullptr);
        QVERIFY(tor->serverConfig.site.isEmpty());
        break;
    }
    case Proto::Sftp: {
        const auto *sftp = std::get_if<SftpProtocolConfig>(&config.protocolConfig.data);
        QVERIFY(sftp != nullptr);
        QCOMPARE(sftp->userName, QString::fromLatin1(protocols::sftp::defaultUserName));
        break;
    }
    case Proto::Socks5Proxy: {
        const auto *socks5 = std::get_if<Socks5ProxyProtocolConfig>(&config.protocolConfig.data);
        QVERIFY(socks5 != nullptr);
        QCOMPARE(socks5->userName, QString::fromLatin1(protocols::socks5Proxy::defaultUserName));
        break;
    }
    default: break;
    }
}

void InstallerConfigTest::defaultConfig_data()
{
    QTest::addColumn<int>("container");
    QTest::addColumn<int>("port");
    QTest::addColumn<int>("transport");
    QTest::addColumn<int>("protocol");
    QTest::addColumn<bool>("portStored");
    QTest::addColumn<QString>("expectedTransport");

    QTest::newRow("awg") << static_cast<int>(DockerContainer::Awg) << 55424 << static_cast<int>(TransportProto::Udp)
                         << static_cast<int>(Proto::Awg) << true << QStringLiteral("udp");
    QTest::newRow("awg2") << static_cast<int>(DockerContainer::Awg2) << 55424 << static_cast<int>(TransportProto::Udp)
                          << static_cast<int>(Proto::Awg) << true << QStringLiteral("udp");
    QTest::newRow("wireguard") << static_cast<int>(DockerContainer::WireGuard) << 51820
                               << static_cast<int>(TransportProto::Udp) << static_cast<int>(Proto::WireGuard) << true
                               << QStringLiteral("udp");
    QTest::newRow("openvpn") << static_cast<int>(DockerContainer::OpenVpn) << 1194
                             << static_cast<int>(TransportProto::Udp) << static_cast<int>(Proto::OpenVpn) << true
                             << QStringLiteral("udp");
    QTest::newRow("xray") << static_cast<int>(DockerContainer::Xray) << 443 << static_cast<int>(TransportProto::Tcp)
                          << static_cast<int>(Proto::Xray) << true << QStringLiteral("tcp");
    // ProtocolUtils currently has no SSXray-specific default port; preserve
    // the resulting -1 until the installer selection contract changes.
    QTest::newRow("ssxray") << static_cast<int>(DockerContainer::SSXray) << -1 << static_cast<int>(TransportProto::Tcp)
                            << static_cast<int>(Proto::SSXray) << true << QStringLiteral("tcp");
    QTest::newRow("tor") << static_cast<int>(DockerContainer::TorWebSite) << -1 << static_cast<int>(TransportProto::Tcp)
                         << static_cast<int>(Proto::TorWebSite) << false << QString();
    QTest::newRow("sftp") << static_cast<int>(DockerContainer::Sftp) << 222 << static_cast<int>(TransportProto::Tcp)
                          << static_cast<int>(Proto::Sftp) << true << QString();
    QTest::newRow("socks5") << static_cast<int>(DockerContainer::Socks5Proxy) << 38080
                            << static_cast<int>(TransportProto::Tcp) << static_cast<int>(Proto::Socks5Proxy) << true
                            << QString();
    QTest::newRow("mtproxy") << static_cast<int>(DockerContainer::MtProxy) << 443
                             << static_cast<int>(TransportProto::Tcp) << static_cast<int>(Proto::MtProxy) << true
                             << QStringLiteral("tcp");
    QTest::newRow("telemt") << static_cast<int>(DockerContainer::Telemt) << 443 << static_cast<int>(TransportProto::Tcp)
                            << static_cast<int>(Proto::Telemt) << true << QStringLiteral("tcp");
    QTest::newRow("amgpt-auth-proxy") << static_cast<int>(DockerContainer::AmgptAuthProxy) << 8080
                                      << static_cast<int>(TransportProto::Tcp)
                                      << static_cast<int>(Proto::AmgptAuthProxy) << true << QStringLiteral("tcp");
    QTest::newRow("openclaw-codex") << static_cast<int>(DockerContainer::OpenClawCodex) << 18789
                                    << static_cast<int>(TransportProto::Tcp) << static_cast<int>(Proto::OpenClawCodex)
                                    << true << QStringLiteral("tcp");
}

void InstallerConfigTest::defaultConfig()
{
    QFETCH(int, container);
    QFETCH(int, port);
    QFETCH(int, transport);
    QFETCH(int, protocol);
    QFETCH(bool, portStored);
    QFETCH(QString, expectedTransport);

    const auto dockerContainer = static_cast<DockerContainer>(container);
    const auto installer = installerFor(dockerContainer);
    const ContainerConfig config =
            installer->generateConfig(dockerContainer, port, static_cast<TransportProto>(transport));
    QCOMPARE(static_cast<int>(config.container), container);
    QCOMPARE(static_cast<int>(config.getProtocolType()), protocol);
    if (portStored)
        QCOMPARE(protocolPort(config), QString::number(port));
    else
        QVERIFY(protocolPort(config).isEmpty());
    QCOMPARE(protocolTransport(config), expectedTransport);
}

void InstallerConfigTest::fallbackContainers_data()
{
    QTest::addColumn<int>("container");
    QTest::addColumn<bool>("unsupported");
    QTest::addColumn<int>("expectedProtocol");

    // Cloak and Shadowsocks are unsupported legacy containers. IPsec, DNS,
    // and None have no concrete installer and therefore use InstallerBase.
    QTest::newRow("cloak-unsupported") << static_cast<int>(DockerContainer::Cloak) << true
                                       << static_cast<int>(Proto::Unknown);
    QTest::newRow("shadowsocks-unsupported")
            << static_cast<int>(DockerContainer::ShadowSocks) << true << static_cast<int>(Proto::Unknown);
    QTest::newRow("ipsec-base-fallback") << static_cast<int>(DockerContainer::Ipsec) << false
                                         << static_cast<int>(Proto::Ikev2);
    QTest::newRow("dns-base-fallback") << static_cast<int>(DockerContainer::Dns) << false << static_cast<int>(Proto::Dns);
    QTest::newRow("none-base-fallback") << static_cast<int>(DockerContainer::None) << false
                                        << static_cast<int>(Proto::Unknown);
}

void InstallerConfigTest::fallbackContainers()
{
    QFETCH(int, container);
    QFETCH(bool, unsupported);
    QFETCH(int, expectedProtocol);

    const auto dockerContainer = static_cast<DockerContainer>(container);
    QCOMPARE(ContainerUtils::isUnsupportedContainer(dockerContainer), unsupported);
    const auto installer = installerFor(dockerContainer);
    const ContainerConfig config = installer->generateConfig(dockerContainer, 1, TransportProto::Tcp);
    QCOMPARE(static_cast<int>(config.container), container);
    // getProtocolType() is container-driven, so containers without a default
    // protocol remain Unknown even though ProtocolConfig has a default variant.
    QCOMPARE(static_cast<int>(config.getProtocolType()), expectedProtocol);
    QVERIFY(protocolPort(config).isEmpty());
    QVERIFY(protocolTransport(config).isEmpty());
}

void InstallerConfigTest::awgGeneratedParameters_data()
{
    QTest::addColumn<int>("container");
    QTest::addColumn<int>("port");

    QTest::newRow("awg") << static_cast<int>(DockerContainer::Awg) << 55424;
    QTest::newRow("awg2") << static_cast<int>(DockerContainer::Awg2) << 1;
}

void InstallerConfigTest::awgGeneratedParameters()
{
    QFETCH(int, container);
    QFETCH(int, port);

    const auto dockerContainer = static_cast<DockerContainer>(container);
    const auto installer = installerFor(dockerContainer);
    const ContainerConfig config = installer->generateConfig(dockerContainer, port, TransportProto::Udp);
    const auto *awg = std::get_if<AwgProtocolConfig>(&config.protocolConfig.data);
    QVERIFY(awg != nullptr);

    const auto &server = awg->serverConfig;
    QCOMPARE(server.port, QString::number(port));
    QCOMPARE(server.transportProto, QStringLiteral("udp"));
    QCOMPARE(server.protocolVersion, QString::fromLatin1(protocols::awg::awgV3));
    QCOMPARE(server.subnetAddress, QString::fromLatin1(protocols::wireguard::defaultSubnetAddress));
    QCOMPARE(server.junkPacketMinSize, QStringLiteral("10"));
    QCOMPARE(server.junkPacketMaxSize, QStringLiteral("50"));
    QCOMPARE(server.initPacketMagicHeader, QStringLiteral("1"));
    QCOMPARE(server.responsePacketMagicHeader, QStringLiteral("2"));
    QCOMPARE(server.underloadPacketMagicHeader, QStringLiteral("3"));
    QCOMPARE(server.transportPacketMagicHeader, QStringLiteral("4"));
    QCOMPARE(server.contentPaddingAddition, QStringLiteral("10-100"));
    QCOMPARE(server.rekeyAfterTime, QStringLiteral("100-120"));
    QCOMPARE(server.rekeyTimeout, QStringLiteral("3-7"));
    QCOMPARE(server.rejectAfterTime, QStringLiteral("150-180"));
    QCOMPARE(server.keepaliveTimeout, QStringLiteral("5-15"));
    QCOMPARE(server.maxHandshakeAttempts, QStringLiteral("15-20"));
    QCOMPARE(server.randomTrailers, QStringLiteral("on"));
    QCOMPARE(server.disableCookies, QStringLiteral("on"));

    bool ok = false;
    const int junkCount = server.junkPacketCount.toInt(&ok);
    QVERIFY(ok);
    QVERIFY(junkCount >= 4 && junkCount <= 6);

    const int initJunk = server.initPacketJunkSize.toInt(&ok);
    QVERIFY(ok);
    QVERIFY(initJunk >= protocols::awg::junkPacketSizeMin && initJunk < protocols::awg::initPacketJunkSizeMax);
    const int responseJunk = server.responsePacketJunkSize.toInt(&ok);
    QVERIFY(ok);
    QVERIFY(responseJunk >= protocols::awg::junkPacketSizeMin && responseJunk < protocols::awg::responsePacketJunkSizeMax);
    const int cookieJunk = server.cookieReplyPacketJunkSize.toInt(&ok);
    QVERIFY(ok);
    QVERIFY(cookieJunk >= protocols::awg::junkPacketSizeMin && cookieJunk < protocols::awg::cookieReplyPacketJunkSizeMax);
    QCOMPARE(server.transportPacketJunkSize, QString::number(protocols::awg::defaultTransportPacketJunkSize));
    QVERIFY(initJunk != responseJunk && initJunk != cookieJunk && responseJunk != cookieJunk);
    QVERIFY(initJunk + AwgConstant::messageInitiationSize != responseJunk + AwgConstant::messageResponseSize);
    QVERIFY(initJunk + AwgConstant::messageInitiationSize != cookieJunk + AwgConstant::messageCookieReplySize);
    QVERIFY(responseJunk + AwgConstant::messageResponseSize != cookieJunk + AwgConstant::messageCookieReplySize);

    const QByteArray headerProtectionKey = QByteArray::fromBase64(server.headerProtectionKey.toUtf8());
    QVERIFY(!server.headerProtectionKey.isEmpty());
    QCOMPARE(headerProtectionKey.size(), 32);

    // Header protection keys are generated secrets. Characterize their size
    // and independent generation without pinning a random value.
    QSet<QByteArray> generatedKeys { headerProtectionKey };
    for (int i = 0; i < 3; ++i) {
        const ContainerConfig nextConfig = installer->generateConfig(dockerContainer, port, TransportProto::Udp);
        const auto *nextAwg = std::get_if<AwgProtocolConfig>(&nextConfig.protocolConfig.data);
        QVERIFY(nextAwg != nullptr);
        const QByteArray nextKey = QByteArray::fromBase64(nextAwg->serverConfig.headerProtectionKey.toUtf8());
        QCOMPARE(nextKey.size(), 32);
        QVERIFY(!generatedKeys.contains(nextKey));
        generatedKeys.insert(nextKey);
    }
    QCOMPARE(generatedKeys.size(), 4);
}

void InstallerConfigTest::generatedCredentials_data()
{
    QTest::addColumn<int>("container");
    QTest::addColumn<int>("port");
    QTest::newRow("sftp") << static_cast<int>(DockerContainer::Sftp) << 222;
    QTest::newRow("socks5") << static_cast<int>(DockerContainer::Socks5Proxy) << 38080;
}

void InstallerConfigTest::generatedCredentials()
{
    QFETCH(int, container);
    QFETCH(int, port);

    const auto dockerContainer = static_cast<DockerContainer>(container);
    const auto installer = installerFor(dockerContainer);
    const QRegularExpression secretAlphabet(QStringLiteral("^[A-Za-z0-9]+$"));
    QSet<QString> generatedPasswords;

    for (int i = 0; i < 8; ++i) {
        const ContainerConfig config = installer->generateConfig(dockerContainer, port, TransportProto::Tcp);
        if (dockerContainer == DockerContainer::Sftp) {
            const auto *sftp = std::get_if<SftpProtocolConfig>(&config.protocolConfig.data);
            QVERIFY(sftp != nullptr);
            QCOMPARE(sftp->userName, QString::fromLatin1(protocols::sftp::defaultUserName));
            QCOMPARE(sftp->port, QString::number(port));
            QVERIFY(!sftp->password.isEmpty());
            QCOMPARE(sftp->password.size(), 16);
            QVERIFY(secretAlphabet.match(sftp->password).hasMatch());
            QVERIFY(!generatedPasswords.contains(sftp->password));
            generatedPasswords.insert(sftp->password);
        } else {
            const auto *socks5 = std::get_if<Socks5ProxyProtocolConfig>(&config.protocolConfig.data);
            QVERIFY(socks5 != nullptr);
            QCOMPARE(socks5->userName, QString::fromLatin1(protocols::socks5Proxy::defaultUserName));
            QCOMPARE(socks5->port, QString::number(port));
            QVERIFY(!socks5->password.isEmpty());
            QCOMPARE(socks5->password.size(), 16);
            QVERIFY(secretAlphabet.match(socks5->password).hasMatch());
            QVERIFY(!generatedPasswords.contains(socks5->password));
            generatedPasswords.insert(socks5->password);
        }
    }
    QCOMPARE(generatedPasswords.size(), 8);
}

QTEST_APPLESS_MAIN(InstallerConfigTest)

#include "installer_config_test.moc"
