#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QTest>
#include <QXmlStreamReader>

#include "core/models/containerConfig.h"
#include "core/models/protocols/amgptAuthProxyProtocolConfig.h"
#include "core/models/protocols/awgProtocolConfig.h"
#include "core/models/protocols/mtProxyProtocolConfig.h"
#include "core/models/protocols/openClawCodexProtocolConfig.h"
#include "core/models/protocols/openVpnProtocolConfig.h"
#include "core/models/protocols/sftpProtocolConfig.h"
#include "core/models/protocols/socks5ProxyProtocolConfig.h"
#include "core/models/protocols/telemtProtocolConfig.h"
#include "core/models/protocols/wireGuardProtocolConfig.h"
#include "core/models/protocols/xrayProtocolConfig.h"
#include "core/protocols/protocolUtils.h"
#include "core/utils/containerEnum.h"
#include "core/utils/containers/containerUtils.h"
#include "core/utils/selfhosted/scriptsRegistry.h"
#include "core/utils/selfhosted/sshSession.h"

using namespace amnezia;

struct ContainerRow
{
    DockerContainer container;
    const char *identity;
    const char *typeIdentity;
    const char *protocolIdentity;
    const char *humanName;
    Proto protocol;
    ServiceType service;
    bool supportedOnThisPlatform;
    bool shareable;
    int installOrder;
    QStringList fixedPorts;
    const char *folder;
    bool hasDescription;
    bool hasDetailedDescription;
    QStringList requiredScripts;
};

Q_DECLARE_METATYPE(ContainerRow)

namespace
{

    bool expectedPlatformSupport(DockerContainer container)
    {
#ifdef Q_OS_WINDOWS
        Q_UNUSED(container);
        return true;
#elif defined(Q_OS_IOS)
        switch (container) {
        case DockerContainer::WireGuard:
        case DockerContainer::OpenVpn:
        case DockerContainer::Awg2:
        case DockerContainer::Awg:
        case DockerContainer::Xray:
        case DockerContainer::SSXray:
        case DockerContainer::MtProxy:
        case DockerContainer::Telemt:
        case DockerContainer::AmgptAuthProxy:
        case DockerContainer::OpenClawCodex: return true;
        default: return false;
        }
#elif defined(MACOS_NE)
        switch (container) {
        case DockerContainer::OpenVpn:
        case DockerContainer::WireGuard:
        case DockerContainer::Awg2:
        case DockerContainer::Awg:
        case DockerContainer::Xray:
        case DockerContainer::SSXray:
        case DockerContainer::MtProxy:
        case DockerContainer::Telemt:
        case DockerContainer::AmgptAuthProxy:
        case DockerContainer::OpenClawCodex: return true;
        default: return false;
        }
#elif defined(Q_OS_MAC)
        return container != DockerContainer::Ipsec;
#elif defined(Q_OS_ANDROID)
        switch (container) {
        case DockerContainer::WireGuard:
        case DockerContainer::OpenVpn:
        case DockerContainer::Awg2:
        case DockerContainer::Awg:
        case DockerContainer::Xray:
        case DockerContainer::SSXray:
        case DockerContainer::MtProxy:
        case DockerContainer::Telemt:
        case DockerContainer::AmgptAuthProxy:
        case DockerContainer::OpenClawCodex: return true;
        default: return false;
        }
#elif defined(Q_OS_LINUX)
        return container != DockerContainer::Ipsec;
#else
        Q_UNUSED(container);
        return false;
#endif
    }

    QList<ContainerRow> containerRows()
    {
        // This table is intentionally exhaustive.  Adding an enum value without
        // classifying it here is a contract failure rather than an implicit skip.
        return {
            { DockerContainer::None,
              "none",
              "none",
              "none",
              "Not installed",
              Proto::Unknown,
              ServiceType::None,
              expectedPlatformSupport(DockerContainer::None),
              true,
              0,
              {},
              "",
              false,
              false,
              {} },
            { DockerContainer::Awg,
              "amnezia-awg",
              "awg",
              "awg",
              "AmneziaWG",
              Proto::Awg,
              ServiceType::Vpn,
              expectedPlatformSupport(DockerContainer::Awg),
              true,
              0,
              {},
              "awg_legacy",
              true,
              false,
              { "Dockerfile", "run_container.sh", "configure_container.sh", "start.sh", "template.conf" } },
            { DockerContainer::Awg2,
              "amnezia-awg2",
              "awg",
              "awg",
              "AmneziaWG",
              Proto::Awg,
              ServiceType::Vpn,
              expectedPlatformSupport(DockerContainer::Awg2),
              true,
              1,
              {},
              "awg",
              true,
              true,
              { "Dockerfile", "run_container.sh", "configure_container.sh", "start.sh", "template.conf" } },
            { DockerContainer::WireGuard,
              "amnezia-wireguard",
              "wireguard",
              "wireguard",
              "WireGuard",
              Proto::WireGuard,
              ServiceType::Vpn,
              expectedPlatformSupport(DockerContainer::WireGuard),
              true,
              2,
              {},
              "wireguard",
              true,
              true,
              { "Dockerfile", "run_container.sh", "configure_container.sh", "start.sh", "template.conf" } },
            { DockerContainer::OpenVpn,
              "amnezia-openvpn",
              "openvpn",
              "openvpn",
              "OpenVPN",
              Proto::OpenVpn,
              ServiceType::Vpn,
              expectedPlatformSupport(DockerContainer::OpenVpn),
              true,
              4,
              {},
              "openvpn",
              true,
              true,
              { "Dockerfile", "run_container.sh", "configure_container.sh", "start.sh", "template.ovpn" } },
            { DockerContainer::Cloak,
              "amnezia-openvpn-cloak",
              "cloak",
              "",
              "OpenVPN over Cloak",
              Proto::Unknown,
              ServiceType::Vpn,
              expectedPlatformSupport(DockerContainer::Cloak),
              false,
              0,
              {},
              "",
              true,
              false,
              {} },
            { DockerContainer::ShadowSocks,
              "amnezia-shadowsocks",
              "shadowsocks",
              "",
              "OpenVPN over SS",
              Proto::Unknown,
              ServiceType::Vpn,
              expectedPlatformSupport(DockerContainer::ShadowSocks),
              false,
              0,
              {},
              "",
              true,
              false,
              {} },
            { DockerContainer::Ipsec,
              "amnezia-ipsec",
              "ikev2",
              "ikev2",
              "IPsec",
              Proto::Ikev2,
              ServiceType::Vpn,
              expectedPlatformSupport(DockerContainer::Ipsec),
              true,
              7,
              { "500", "4500" },
              "ipsec",
              true,
              true,
              { "Dockerfile", "run_container.sh", "configure_container.sh", "start.sh" } },
            { DockerContainer::Xray,
              "amnezia-xray",
              "xray",
              "xray",
              "XRay",
              Proto::Xray,
              ServiceType::Vpn,
              expectedPlatformSupport(DockerContainer::Xray),
              true,
              3,
              {},
              "xray",
              true,
              true,
              { "Dockerfile", "run_container.sh", "configure_container.sh", "start.sh", "template.json" } },
            // SSXray deliberately shares Xray's deployment assets and installer.
            { DockerContainer::SSXray,
              "amnezia-ssxray",
              "ssxray",
              "ssxray",
              "Shadowsocks",
              Proto::SSXray,
              ServiceType::None,
              expectedPlatformSupport(DockerContainer::SSXray),
              true,
              8,
              {},
              "xray",
              false,
              false,
              { "Dockerfile", "run_container.sh", "configure_container.sh", "start.sh", "template.json" } },
            { DockerContainer::TorWebSite,
              "amnezia-torwebsite",
              "torwebsite",
              "torwebsite",
              "Website in Tor network",
              Proto::TorWebSite,
              ServiceType::Other,
              expectedPlatformSupport(DockerContainer::TorWebSite),
              false,
              0,
              {},
              "website_tor",
              true,
              true,
              { "Dockerfile", "run_container.sh", "configure_container.sh" } },
            { DockerContainer::Dns,
              "amnezia-dns",
              "dns",
              "dns",
              "AmneziaDNS",
              Proto::Dns,
              ServiceType::Other,
              expectedPlatformSupport(DockerContainer::Dns),
              false,
              0,
              {},
              "dns",
              true,
              true,
              { "Dockerfile", "run_container.sh", "configure_container.sh" } },
            { DockerContainer::Sftp,
              "amnezia-sftp",
              "sftp",
              "sftp",
              "SFTP file sharing service",
              Proto::Sftp,
              ServiceType::Other,
              expectedPlatformSupport(DockerContainer::Sftp),
              false,
              0,
              {},
              "sftp",
              true,
              true,
              { "Dockerfile", "run_container.sh", "configure_container.sh" } },
            { DockerContainer::Socks5Proxy,
              "amnezia-socks5proxy",
              "socks5proxy",
              "socks5proxy",
              "SOCKS5 proxy server",
              Proto::Socks5Proxy,
              ServiceType::Other,
              expectedPlatformSupport(DockerContainer::Socks5Proxy),
              false,
              0,
              {},
              "socks5_proxy",
              true,
              true,
              { "Dockerfile", "run_container.sh", "configure_container.sh", "start.sh" } },
            { DockerContainer::MtProxy,
              "amnezia-mtproxy",
              "mtproxy",
              "mtproxy",
              "MTProxy (Telegram)",
              Proto::MtProxy,
              ServiceType::Other,
              expectedPlatformSupport(DockerContainer::MtProxy),
              false,
              20,
              {},
              "mtproxy",
              true,
              true,
              { "Dockerfile", "run_container.sh", "configure_container.sh", "start.sh" } },
            { DockerContainer::Telemt,
              "amnezia-telemt",
              "telemt",
              "telemt",
              "Telemt (Telegram)",
              Proto::Telemt,
              ServiceType::Other,
              expectedPlatformSupport(DockerContainer::Telemt),
              false,
              20,
              {},
              "telemt",
              true,
              true,
              { "Dockerfile", "run_container.sh", "configure_container.sh", "start.sh" } },
            { DockerContainer::AmgptAuthProxy,
              "amnezia-amgpt-device-gateway",
              "amgpt-device-gateway",
              "amgptdevicegateway",
              "AMGPT Device Gateway",
              Proto::AmgptAuthProxy,
              ServiceType::Other,
              expectedPlatformSupport(DockerContainer::AmgptAuthProxy),
              false,
              21,
              {},
              "amgpt-device-gateway",
              true,
              true,
              { "Dockerfile", "run_container.sh" } },
            { DockerContainer::OpenClawCodex,
              "amnezia-openclaw-codex",
              "openclaw-codex",
              "openclawcodex",
              "OpenClaw + Codex",
              Proto::OpenClawCodex,
              ServiceType::Other,
              expectedPlatformSupport(DockerContainer::OpenClawCodex),
              false,
              22,
              {},
              "openclaw-codex",
              true,
              true,
              { "Dockerfile", "run_container.sh" } },
        };
    }

    QString sourceRoot()
    {
#ifdef CONTRACT_SOURCE_ROOT
        return QStringLiteral(CONTRACT_SOURCE_ROOT);
#else
        return QDir::currentPath();
#endif
    }

    QString qrcPath()
    {
        return sourceRoot() + QStringLiteral("/client/server_scripts/serverScripts.qrc");
    }

    QString assetRoot()
    {
        return sourceRoot() + QStringLiteral("/client/server_scripts");
    }

    QStringList qrcEntries(QString *error = nullptr)
    {
        QFile file(qrcPath());
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) {
                *error = file.errorString();
            }
            return {};
        }

        QXmlStreamReader xml(&file);
        QStringList entries;
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement() || xml.name() != QLatin1String("file")) {
                continue;
            }
            const QString alias = xml.attributes().value(QStringLiteral("alias")).toString();
            const QString value = xml.readElementText(QXmlStreamReader::SkipChildElements);
            entries.append(alias.isEmpty() ? value : alias);
        }
        if (xml.hasError() && error) {
            *error = xml.errorString();
        }
        return entries;
    }

    QStringList diskAssets()
    {
        QStringList assets;
        QDirIterator iterator(assetRoot(), { QStringLiteral("*") }, QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            const QString relative = QDir(assetRoot()).relativeFilePath(path);
            if (relative != QLatin1String("serverScripts.qrc")) {
                assets.append(relative);
            }
        }
        assets.sort();
        return assets;
    }

    QStringList mapKeys(const ScriptVars &vars)
    {
        QStringList keys;
        for (const auto &entry : vars) {
            keys.append(entry.first);
        }
        return keys;
    }

    QString variable(const ScriptVars &vars, const QString &key)
    {
        for (const auto &entry : vars) {
            if (entry.first == key) {
                return entry.second;
            }
        }
        return QString();
    }

    void assertVariable(const ScriptVars &vars, const QString &key, const QString &expected)
    {
        // Keep values out of assertion text: some production variable families
        // contain passwords, secrets, or private keys.
        QVERIFY2(mapKeys(vars).contains(key), "expected generated variable is missing");
        QVERIFY2(variable(vars, key) == expected, "generated variable has an unexpected value");
    }
} // namespace

class ContainerRegistryContractTest : public QObject
{
    Q_OBJECT

private slots:
    void deviceGatewayOwnsTheOnlyLoginUi()
    {
        QFile file(sourceRoot() + QStringLiteral("/client/ui/qml/Pages2/PageServiceAgentWorkloadSettings.qml"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray source = file.readAll();
        QVERIFY(source.contains(
            "property bool isDeviceGateway: savedConfig.container === \"amnezia-amgpt-device-gateway\""));
        QVERIFY(source.contains("visible: root.isDeviceGateway && root.currentAction === root.actionNoOp"));
        QVERIFY(source.contains("property int selectedLoginMode: loginAmgpt"));
        QCOMPARE(source.count("Sign in with Amnezia GPT"), 1);
        QVERIFY(!source.contains("Sign in with ChatGPT"));
        QVERIFY(!source.contains("nativeLoginEnabled"));
        QVERIFY(!source.contains("Open Amnezia GPT Proxy"));
    }
    void enumRowsAreClassified_data();
    void enumRowsAreClassified();
    void everyEnumValueHasAContractRow();
    void unsupportedContainersRemainExplicit();
    void registryFoldersAndScriptsAreEmbedded_data();
    void registryFoldersAndScriptsAreEmbedded();
    void sharedRegistryScriptsAreEmbedded();
    void qrcAndDiskAssetsHaveTheSameManifest();
    void shellAssetsPassSyntaxChecks();
    void substitutionsPreserveDataAndLeaveMissingPlaceholders();
    void protocolVariableGeneratorsUseDeterministicFixtures();
    void agentWorkloadConfigsRoundTripIndependently();
    void agentWorkloadDesiredStateFlowsThroughScriptRegistry();
    void localBuildEntrypointsBoundParallelism();
};

void ContainerRegistryContractTest::enumRowsAreClassified_data()
{
    QTest::addColumn<ContainerRow>("row");
    for (const ContainerRow &row : containerRows()) {
        QTest::newRow(row.identity) << row;
    }
}

void ContainerRegistryContractTest::enumRowsAreClassified()
{
    QFETCH(ContainerRow, row);

    const auto all = ContainerUtils::allContainers();
    QVERIFY2(all.contains(row.container), "contract table row is not in the production enum");
    QCOMPARE(ContainerUtils::containerToString(row.container), QString::fromLatin1(row.identity));
    QCOMPARE(ContainerUtils::containerFromString(QString::fromLatin1(row.identity)), row.container);
    QCOMPARE(ContainerUtils::containerTypeToString(row.container), QString::fromLatin1(row.typeIdentity));
    QCOMPARE(ContainerUtils::containerTypeToProtocolString(row.container), QString::fromLatin1(row.protocolIdentity));
    QCOMPARE(ContainerUtils::containerHumanNames().value(row.container), QString::fromLatin1(row.humanName));
    QCOMPARE(ContainerUtils::defaultProtocol(row.container), row.protocol);
    QCOMPARE(ContainerUtils::containerService(row.container), row.service);
    QCOMPARE(ContainerUtils::isSupportedByCurrentPlatform(row.container), row.supportedOnThisPlatform);
    QCOMPARE(ContainerUtils::isShareable(row.container), row.shareable);
    QCOMPARE(ContainerUtils::installPageOrder(row.container), row.installOrder);
    QCOMPARE(ContainerUtils::fixedPortsForContainer(row.container), row.fixedPorts);
    QCOMPARE(ContainerUtils::isUnsupportedContainer(row.container),
             row.container == DockerContainer::Cloak || row.container == DockerContainer::ShadowSocks);

    const auto descriptions = ContainerUtils::containerDescriptions();
    QCOMPARE(descriptions.contains(row.container), row.hasDescription);
    // SOCKS5 is intentionally registered with an empty description in the
    // current metadata table; preserve that characterization without treating
    // it as a missing map entry.
    if (row.hasDescription && row.container != DockerContainer::Socks5Proxy) {
        QVERIFY(!descriptions.value(row.container).isEmpty());
    }
    const auto detailedDescriptions = ContainerUtils::containerDetailedDescriptions();
    QCOMPARE(detailedDescriptions.contains(row.container), row.hasDetailedDescription);
    if (row.hasDetailedDescription) {
        QVERIFY(!detailedDescriptions.value(row.container).isEmpty());
    }
}

void ContainerRegistryContractTest::everyEnumValueHasAContractRow()
{
    const QList<ContainerRow> rows = containerRows();
    const QList<DockerContainer> all = ContainerUtils::allContainers();
    QCOMPARE(all.size(), rows.size());
    for (const DockerContainer container : all) {
        bool classified = false;
        for (const ContainerRow &row : rows) {
            if (row.container == container) {
                classified = true;
                break;
            }
        }
        QVERIFY2(classified, "production DockerContainer value has no contract classification row");
    }
}

void ContainerRegistryContractTest::unsupportedContainersRemainExplicit()
{
    for (const DockerContainer container : { DockerContainer::Cloak, DockerContainer::ShadowSocks }) {
        QVERIFY(ContainerUtils::isUnsupportedContainer(container));
        QCOMPARE(ContainerUtils::defaultProtocol(container), Proto::Unknown);
        QCOMPARE(ContainerUtils::containerService(container), ServiceType::Vpn);
        QVERIFY(!ContainerUtils::isShareable(container));
        QVERIFY(scriptFolder(container).isEmpty());
        QVERIFY(scriptData(ProtocolScriptType::dockerfile, container).isEmpty());
    }

    QCOMPARE(scriptFolder(DockerContainer::Xray), QStringLiteral("xray"));
    QCOMPARE(scriptFolder(DockerContainer::SSXray), QStringLiteral("xray"));
}

void ContainerRegistryContractTest::registryFoldersAndScriptsAreEmbedded_data()
{
    QTest::addColumn<ContainerRow>("row");
    for (const ContainerRow &row : containerRows()) {
        if (row.folder[0] != '\0') {
            QTest::newRow(row.identity) << row;
        }
    }
}

void ContainerRegistryContractTest::registryFoldersAndScriptsAreEmbedded()
{
    QFETCH(ContainerRow, row);
    QCOMPARE(scriptFolder(row.container), QString::fromLatin1(row.folder));
    for (const QString &script : row.requiredScripts) {
        ProtocolScriptType type;
        if (script == QLatin1String("Dockerfile")) {
            type = ProtocolScriptType::dockerfile;
        } else if (script == QLatin1String("run_container.sh")) {
            type = ProtocolScriptType::run_container;
        } else if (script == QLatin1String("configure_container.sh")) {
            type = ProtocolScriptType::configure_container;
        } else if (script == QLatin1String("start.sh")) {
            type = ProtocolScriptType::container_startup;
        } else if (script == QLatin1String("template.ovpn")) {
            type = ProtocolScriptType::openvpn_template;
        } else if (script == QLatin1String("template.json")) {
            type = ProtocolScriptType::xray_template;
        } else {
            type = row.protocol == Proto::WireGuard ? ProtocolScriptType::wireguard_template
                                                    : ProtocolScriptType::awg_template;
        }
        const QString resourcePath = QStringLiteral(":/server_scripts/") + row.folder + QLatin1Char('/') + script;
        QFile embedded(resourcePath);
        QVERIFY2(embedded.open(QIODevice::ReadOnly), "required registry asset is missing from the embedded resource");
        QByteArray expected = embedded.readAll();
        expected.replace("\r", "");
        const QString data = scriptData(type, row.container);
        QVERIFY2(data == QString::fromUtf8(expected), "registry returned unexpected embedded asset contents");
        QVERIFY2(QFile::exists(resourcePath), "required registry asset does not resolve in the Qt resource system");
    }
}

void ContainerRegistryContractTest::sharedRegistryScriptsAreEmbedded()
{
    const QList<QPair<SharedScriptType, QString>> sharedScripts = {
        { SharedScriptType::prepare_host, QStringLiteral("prepare_host.sh") },
        { SharedScriptType::install_docker, QStringLiteral("install_docker.sh") },
        { SharedScriptType::install_conntrack, QStringLiteral("install_conntrack.sh") },
        { SharedScriptType::build_container, QStringLiteral("build_container.sh") },
        { SharedScriptType::remove_container, QStringLiteral("remove_container.sh") },
        { SharedScriptType::remove_all_containers, QStringLiteral("remove_all_containers.sh") },
        { SharedScriptType::setup_host_firewall, QStringLiteral("setup_host_firewall.sh") },
        { SharedScriptType::check_connection, QStringLiteral("check_connection.sh") },
        { SharedScriptType::check_server_is_busy, QStringLiteral("check_server_is_busy.sh") },
        { SharedScriptType::check_user_in_sudo, QStringLiteral("check_user_in_sudo.sh") },
    };

    for (const auto &entry : sharedScripts) {
        QCOMPARE(scriptName(entry.first), entry.second);
        const QString resourcePath = QStringLiteral(":/server_scripts/") + entry.second;
        QFile embedded(resourcePath);
        QVERIFY2(embedded.open(QIODevice::ReadOnly), "shared registry asset is missing from the embedded resource");
        QCOMPARE(scriptData(entry.first), QString::fromUtf8(embedded.readAll()));
    }
}

void ContainerRegistryContractTest::qrcAndDiskAssetsHaveTheSameManifest()
{
    QString error;
    QStringList qrc = qrcEntries(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    qrc.sort();
    QVERIFY2(!qrc.isEmpty(), "serverScripts.qrc has no entries");
    QCOMPARE(qrc.size(), QSet<QString>(qrc.cbegin(), qrc.cend()).size());

    const QStringList disk = diskAssets();
    QVERIFY2(!disk.isEmpty(), "no deployment assets found on disk");
    const QSet<QString> qrcSet(qrc.cbegin(), qrc.cend());
    const QSet<QString> diskSet(disk.cbegin(), disk.cend());
    const QSet<QString> missingOnDisk = qrcSet - diskSet;
    QVERIFY2(missingOnDisk.isEmpty(), "serverScripts.qrc references an asset absent from disk");
    const QSet<QString> missingInQrc = diskSet - qrcSet;
    // check_server.sh is a standalone server-diagnostics utility. It is not
    // addressed by scriptsRegistry and is intentionally not bundled into the
    // client resource tree. Keep that exception explicit so any new orphaned
    // deployment asset still fails this contract.
    const QSet<QString> standaloneDiskAssets = { QStringLiteral("check_server.sh") };
    QCOMPARE(missingInQrc, standaloneDiskAssets);
    for (const QString &entry : qrc) {
        QVERIFY2(QFile::exists(QStringLiteral(":/server_scripts/") + entry),
                 "serverScripts.qrc entry is not available in the embedded Qt resource");
    }
}

void ContainerRegistryContractTest::shellAssetsPassSyntaxChecks()
{
#ifdef Q_OS_WINDOWS
    QSKIP("POSIX shell interpreters are not required on Windows");
#else
    const QString bash = QStandardPaths::findExecutable(QStringLiteral("bash"));
    const QString sh = QStandardPaths::findExecutable(QStringLiteral("sh"));

    for (const QString &relative : diskAssets()) {
        if (!relative.endsWith(QLatin1String(".sh"))) {
            continue;
        }
        const QString path = assetRoot() + QLatin1Char('/') + relative;
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray firstLine = file.readLine();
        const bool requestsBash = firstLine.contains("/bin/bash") || firstLine.contains("/usr/bin/env bash");
        const bool requestsSh = firstLine.contains("/bin/sh") || firstLine.contains("/usr/bin/env sh");
        // Never parse bash syntax with sh: a valid bash script can otherwise
        // fail this gate on minimal hosts. Scripts without a specific shebang
        // may use either available POSIX shell.
        const QString interpreter = requestsBash ? bash : (requestsSh ? sh : (sh.isEmpty() ? bash : sh));
        if (interpreter.isEmpty()) {
            QSKIP("the interpreter required by a first-party shell asset is unavailable");
        }

        QProcess process;
        process.start(interpreter, { QStringLiteral("-n"), path });
        QVERIFY2(process.waitForFinished(5000), "shell syntax checker did not finish");
        QVERIFY2(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
                 "first-party shell asset failed syntax checking");
    }
#endif
}

void ContainerRegistryContractTest::substitutionsPreserveDataAndLeaveMissingPlaceholders()
{
    ServerCredentials credentials;
    credentials.hostName = QStringLiteral("host;$(printf untouched)");
    credentials.userName = QStringLiteral("fixture-user");
    credentials.secretData = QStringLiteral("fixture-secret");

    const ScriptVars vars = genBaseVars(credentials, DockerContainer::Xray, QStringLiteral("dns;$(printf primary)"),
                                        QStringLiteral("dns|secondary"));
    assertVariable(vars, QStringLiteral("$REMOTE_HOST"), credentials.hostName);
    assertVariable(vars, QStringLiteral("$CONTAINER_NAME"), QStringLiteral("amnezia-xray"));
    assertVariable(vars, QStringLiteral("$SERVER_IP_ADDRESS"), credentials.hostName);
    assertVariable(vars, QStringLiteral("$PRIMARY_SERVER_DNS"), QStringLiteral("dns;$(printf primary)"));
    assertVariable(vars, QStringLiteral("$SECONDARY_SERVER_DNS"), QStringLiteral("dns|secondary"));

    const QString script = QStringLiteral("before $REMOTE_HOST / $MISSING_PLACEHOLDER / $PRIMARY_SERVER_DNS after");
    const QString substituted = SshSession::replaceVars(script, vars);
    QCOMPARE(substituted,
             QStringLiteral("before host;$(printf untouched) / $MISSING_PLACEHOLDER / dns;$(printf primary) after"));
}

void ContainerRegistryContractTest::protocolVariableGeneratorsUseDeterministicFixtures()
{
    ContainerConfig openVpn;
    OpenVpnProtocolConfig openVpnConfig;
    openVpnConfig.serverConfig.port = QStringLiteral("11940");
    openVpnConfig.serverConfig.transportProto = QStringLiteral("TCP");
    openVpnConfig.serverConfig.additionalClientConfig = QStringLiteral("fixture-client;value");
    openVpn.protocolConfig = openVpnConfig;
    const ScriptVars openVpnVars = genOpenVpnVars(openVpn);
    assertVariable(openVpnVars, QStringLiteral("$OPENVPN_PORT"), QStringLiteral("11940"));
    assertVariable(openVpnVars, QStringLiteral("$OPENVPN_TRANSPORT_PROTO"), QStringLiteral("TCP"));
    assertVariable(openVpnVars, QStringLiteral("$OPENVPN_ADDITIONAL_CLIENT_CONFIG"),
                   QStringLiteral("fixture-client;value"));

    ContainerConfig xray;
    XrayProtocolConfig xrayConfig;
    xrayConfig.serverConfig.port = QStringLiteral("8443");
    xrayConfig.serverConfig.site = QStringLiteral("example.test;$(printf site)");
    xray.protocolConfig = xrayConfig;
    const ScriptVars xrayVars = genXrayVars(xray);
    assertVariable(xrayVars, QStringLiteral("$XRAY_SERVER_PORT"), QStringLiteral("8443"));
    assertVariable(xrayVars, QStringLiteral("$XRAY_SITE_NAME"), QStringLiteral("example.test;$(printf site)"));

    ContainerConfig wireGuard;
    WireGuardProtocolConfig wireGuardConfig;
    wireGuardConfig.serverConfig.port = QStringLiteral("51821");
    wireGuard.protocolConfig = wireGuardConfig;
    assertVariable(genWireGuardVars(wireGuard), QStringLiteral("$WIREGUARD_SERVER_PORT"), QStringLiteral("51821"));

    ContainerConfig awg;
    AwgProtocolConfig awgConfig;
    awgConfig.serverConfig.port = QStringLiteral("51822");
    awgConfig.serverConfig.junkPacketCount = QStringLiteral("3");
    awg.protocolConfig = awgConfig;
    assertVariable(genAwgVars(awg), QStringLiteral("$AWG_SERVER_PORT"), QStringLiteral("51822"));
    assertVariable(genAwgVars(awg), QStringLiteral("$JUNK_PACKET_COUNT"), QStringLiteral("3"));

    ContainerConfig sftp;
    SftpProtocolConfig sftpConfig;
    sftpConfig.port = QStringLiteral("2222");
    sftpConfig.userName = QStringLiteral("fixture-user");
    sftp.protocolConfig = sftpConfig;
    assertVariable(genSftpVars(sftp), QStringLiteral("$SFTP_PORT"), QStringLiteral("2222"));
    assertVariable(genSftpVars(sftp), QStringLiteral("$SFTP_USER"), QStringLiteral("fixture-user"));

    ContainerConfig socks;
    Socks5ProxyProtocolConfig socksConfig;
    socksConfig.userName = QStringLiteral("fixture-user");
    socksConfig.password = QStringLiteral("fixture-password;value");
    socks.protocolConfig = socksConfig;
    const ScriptVars socksVars = genSocks5ProxyVars(socks);
    assertVariable(socksVars, QStringLiteral("$SOCKS5_AUTH_TYPE"), QStringLiteral("strong"));
    QVERIFY2(variable(socksVars, QStringLiteral("$SOCKS5_USER")).contains(QStringLiteral("fixture-password;value")),
             "proxy credentials were not carried into the generated fixture");

    ContainerConfig mtProxy;
    MtProxyProtocolConfig mtProxyConfig;
    mtProxyConfig.transportMode = QStringLiteral("faketls");
    mtProxyConfig.additionalSecrets = { QStringLiteral("fixture-a"), QString(), QStringLiteral("fixture-b") };
    mtProxy.protocolConfig = mtProxyConfig;
    const ScriptVars mtProxyVars = genMtProxyVars(mtProxy);
    assertVariable(mtProxyVars, QStringLiteral("$MTPROXY_WORKERS"), QStringLiteral("0"));
    assertVariable(mtProxyVars, QStringLiteral("$MTPROXY_ADDITIONAL_SECRETS"), QStringLiteral("fixture-a,fixture-b"));

    ContainerConfig telemt;
    TelemtProtocolConfig telemtConfig;
    telemtConfig.transportMode = QStringLiteral("faketls");
    telemtConfig.natEnabled = true;
    telemtConfig.natExternalIp = QStringLiteral("192.0.2.99");
    telemt.protocolConfig = telemtConfig;
    const ScriptVars telemtVars = genTelemtVars(telemt);
    assertVariable(telemtVars, QStringLiteral("$TELEMT_TOML_TLS"), QStringLiteral("true"));
    assertVariable(telemtVars, QStringLiteral("$TELEMT_MIDDLE_PROXY_NAT_IP"), QStringLiteral("192.0.2.99"));

    const ScriptVars xrayContainerVars = genProtocolVarsForContainer(DockerContainer::Xray, xray);
    assertVariable(xrayContainerVars, QStringLiteral("$XRAY_SERVER_PORT"), QStringLiteral("8443"));
    const ScriptVars ssXrayContainerVars = genProtocolVarsForContainer(DockerContainer::SSXray, xray);
    assertVariable(ssXrayContainerVars, QStringLiteral("$XRAY_SERVER_PORT"), QStringLiteral("8443"));
}

void ContainerRegistryContractTest::agentWorkloadConfigsRoundTripIndependently()
{
    ContainerConfig proxy;
    proxy.container = DockerContainer::AmgptAuthProxy;
    proxy.protocolConfig = AmgptAuthProxyProtocolConfig {
        QStringLiteral("18080"),
        QStringLiteral("development"),
        QStringLiteral("https://auth-dev.example.com"),
        QStringLiteral("https://router-dev.example.com/v1"),
        QStringLiteral("https://runtime-dev.example.com"),
    };

    const QJsonObject proxyJson = proxy.toJson();
    QCOMPARE(proxyJson.value(QStringLiteral("container")).toString(), QStringLiteral("amnezia-amgpt-device-gateway"));
    QVERIFY(proxyJson.contains(QStringLiteral("amgptdevicegateway")));
    QVERIFY(!proxyJson.contains(QStringLiteral("openclawcodex")));
    const ContainerConfig restoredProxy = ContainerConfig::fromJson(proxyJson);
    QCOMPARE(restoredProxy.container, DockerContainer::AmgptAuthProxy);
    QCOMPARE(restoredProxy.protocolConfig.type(), Proto::AmgptAuthProxy);
    // A legacy persisted port round-trips but is never reported as a published port.
    QVERIFY(restoredProxy.protocolConfig.port().isEmpty());
    QCOMPARE(restoredProxy.protocolConfig.transportProto(), QStringLiteral("tcp"));
    const auto *restoredProxyConfig = restoredProxy.getAmgptAuthProxyProtocolConfig();
    QVERIFY(restoredProxyConfig != nullptr);
    QCOMPARE(restoredProxyConfig->port, QStringLiteral("18080"));
    QCOMPARE(restoredProxyConfig->backendProfile, QStringLiteral("development"));
    QCOMPARE(restoredProxyConfig->authIssuer, QStringLiteral("https://auth-dev.example.com"));
    QCOMPARE(restoredProxyConfig->routerBaseUrl, QStringLiteral("https://router-dev.example.com/v1"));
    QCOMPARE(restoredProxyConfig->runtimeGatewayBaseUrl, QStringLiteral("https://runtime-dev.example.com"));

    ContainerConfig workload;
    workload.container = DockerContainer::OpenClawCodex;
    workload.protocolConfig = OpenClawCodexProtocolConfig { QStringLiteral("28789") };

    const QJsonObject workloadJson = workload.toJson();
    QCOMPARE(workloadJson.value(QStringLiteral("container")).toString(), QStringLiteral("amnezia-openclaw-codex"));
    QVERIFY(workloadJson.contains(QStringLiteral("openclawcodex")));
    QVERIFY(!workloadJson.contains(QStringLiteral("amgptdevicegateway")));
    const ContainerConfig restoredWorkload = ContainerConfig::fromJson(workloadJson);
    QCOMPARE(restoredWorkload.container, DockerContainer::OpenClawCodex);
    QCOMPARE(restoredWorkload.protocolConfig.type(), Proto::OpenClawCodex);
    QVERIFY(restoredWorkload.protocolConfig.port().isEmpty());
    QCOMPARE(restoredWorkload.protocolConfig.transportProto(), QStringLiteral("tcp"));

    // Agent workloads publish no host port, so the installer offers none.
    QCOMPARE(ProtocolUtils::defaultPort(Proto::AmgptAuthProxy), -1);
    QCOMPARE(ProtocolUtils::defaultPort(Proto::OpenClawCodex), -1);
    QVERIFY(ProtocolUtils::allProtocols().contains(Proto::AmgptAuthProxy));
    QVERIFY(ProtocolUtils::allProtocols().contains(Proto::OpenClawCodex));
    QCOMPARE(ProtocolUtils::protoFromString(QStringLiteral("amgptdevicegateway")), Proto::AmgptAuthProxy);
    QCOMPARE(ProtocolUtils::protoFromString(QStringLiteral("openclawcodex")), Proto::OpenClawCodex);
    QCOMPARE(ProtocolUtils::defaultTransportProto(Proto::AmgptAuthProxy), TransportProto::Tcp);
    QCOMPARE(ProtocolUtils::defaultTransportProto(Proto::OpenClawCodex), TransportProto::Tcp);
    QVERIFY(!ProtocolUtils::defaultPortChangeable(Proto::AmgptAuthProxy));
    QVERIFY(!ProtocolUtils::defaultPortChangeable(Proto::OpenClawCodex));
}

void ContainerRegistryContractTest::agentWorkloadDesiredStateFlowsThroughScriptRegistry()
{
    ContainerConfig proxy;
    proxy.container = DockerContainer::AmgptAuthProxy;
    proxy.protocolConfig = AmgptAuthProxyProtocolConfig {
        QStringLiteral("18080"),
        QStringLiteral("development"),
        QStringLiteral("https://auth-dev.example.com"),
        QStringLiteral("https://router-dev.example.com/v1"),
        QStringLiteral("https://runtime-dev.example.com"),
    };

    const ScriptVars proxyVars = genProtocolVarsForContainer(DockerContainer::AmgptAuthProxy, proxy);
    QVERIFY(!mapKeys(proxyVars).contains(QStringLiteral("$AMGPT_DEVICE_GATEWAY_PORT")));
    assertVariable(proxyVars, QStringLiteral("$AMGPT_AUTH_ISSUER"), QStringLiteral("https://auth-dev.example.com"));
    assertVariable(proxyVars, QStringLiteral("$AMGPT_ROUTER_BASE_URL"),
                   QStringLiteral("https://router-dev.example.com/v1"));
    assertVariable(proxyVars, QStringLiteral("$AMGPT_RUNTIME_GATEWAY_BASE_URL"), QStringLiteral("https://runtime-dev.example.com"));
    assertVariable(proxyVars, QStringLiteral("$AGENT_BACKEND_PROFILE"), QStringLiteral("development"));
    assertVariable(proxyVars, QStringLiteral("$AGENT_WORKLOAD_ID"), QStringLiteral("amgpt-device-gateway"));
    QCOMPARE(variable(proxyVars, QStringLiteral("$AGENT_DEPLOYMENT_SPEC_HASH")).size(), 64);

    ContainerConfig openClaw;
    openClaw.container = DockerContainer::OpenClawCodex;
    openClaw.protocolConfig = OpenClawCodexProtocolConfig { QStringLiteral("28789") };

    const ScriptVars openClawVars = genProtocolVarsForContainer(DockerContainer::OpenClawCodex, openClaw);
    QVERIFY(!mapKeys(openClawVars).contains(QStringLiteral("$OPENCLAW_CODEX_PORT")));
    assertVariable(openClawVars, QStringLiteral("$AGENT_WORKLOAD_ID"), QStringLiteral("openclaw-codex"));
    QVERIFY(!mapKeys(openClawVars).contains(QStringLiteral("$AMGPT_AUTH_ISSUER")));
    QVERIFY(!mapKeys(openClawVars).contains(QStringLiteral("$AMGPT_ROUTER_BASE_URL")));
    QVERIFY(!mapKeys(openClawVars).contains(QStringLiteral("$AMGPT_RUNTIME_GATEWAY_BASE_URL")));
    QVERIFY(!mapKeys(openClawVars).contains(QStringLiteral("$AGENT_BACKEND_PROFILE")));
}

void ContainerRegistryContractTest::localBuildEntrypointsBoundParallelism()
{
    QFile presetsFile(sourceRoot() + QStringLiteral("/CMakePresets.json"));
    QVERIFY(presetsFile.open(QIODevice::ReadOnly));
    const QJsonDocument presetsDocument = QJsonDocument::fromJson(presetsFile.readAll());
    QVERIFY(presetsDocument.isObject());
    const QJsonArray buildPresets = presetsDocument.object().value(QStringLiteral("buildPresets")).toArray();
    QMap<QString, int> localJobs;
    for (const QJsonValue &value : buildPresets) {
        const QJsonObject preset = value.toObject();
        const QString name = preset.value(QStringLiteral("name")).toString();
        if (name == QStringLiteral("local") || name == QStringLiteral("local-client")) {
            localJobs.insert(name, preset.value(QStringLiteral("jobs")).toInt());
        }
    }
    QCOMPARE(localJobs.value(QStringLiteral("local")), 2);
    QCOMPARE(localJobs.value(QStringLiteral("local-client")), 2);

    QFile qualityFile(sourceRoot() + QStringLiteral("/just/quality.just"));
    QVERIFY(qualityFile.open(QIODevice::ReadOnly));
    const QByteArray quality = qualityFile.readAll();
    const QList<QByteArray> qualityLines = quality.split('\n');
    int buildCommandCount = 0;
    for (const QByteArray &line : qualityLines) {
        if (!line.contains("cmake --build")) {
            continue;
        }
        ++buildCommandCount;
        QVERIFY2(line.contains("--parallel \"{{local_jobs}}\""),
                 "a quality build entrypoint does not explicitly bound parallelism");
    }
    QCOMPARE(buildCommandCount, 4);
    QVERIFY(quality.contains("env_var_or_default(\"AMNEZIA_BUILD_JOBS\", \"2\")"));

    QFile buildScript(sourceRoot() + QStringLiteral("/deploy/build.sh"));
    QVERIFY(buildScript.open(QIODevice::ReadOnly));
    const QByteArray buildScriptData = buildScript.readAll();
    QVERIFY(buildScriptData.contains("JOBS=${AMNEZIA_BUILD_JOBS:-2}"));
    QVERIFY(buildScriptData.contains("if [[ -n \"${CI:-}\" ]]"));
    QVERIFY(buildScriptData.contains("--parallel \"$JOBS\""));
}

QTEST_APPLESS_MAIN(ContainerRegistryContractTest)

#include "container_registry_contract_test.moc"
