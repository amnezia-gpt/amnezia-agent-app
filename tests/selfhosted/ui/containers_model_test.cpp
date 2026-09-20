#include <QAbstractItemModelTester>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

#include <variant>

#include "core/models/containerConfig.h"
#include "core/models/protocols/awgProtocolConfig.h"
#include "core/models/protocols/sftpProtocolConfig.h"
#include "core/utils/containerEnum.h"
#include "core/utils/containers/containerUtils.h"
#include "core/utils/protocolEnum.h"
#include "ui/models/containersModel.h"

using namespace amnezia;

namespace
{
    class InspectableContainersModel : public ContainersModel
    {
    public:
        using ContainersModel::roleNames;
    };

    int rowForContainer(const ContainersModel &model, DockerContainer container)
    {
        for (int row = 0; row < model.rowCount(); ++row) {
            if (model.data(model.index(row, 0), ContainersModel::DockerContainerRole).toInt()
                == static_cast<int>(container)) {
                return row;
            }
        }
        return -1;
    }

    ContainerConfig makeThirdPartyAwgContainerConfig()
    {
        ContainerConfig config;
        config.container = DockerContainer::Awg;

        AwgProtocolConfig protocolConfig;
        protocolConfig.serverConfig.port = QStringLiteral("55424");
        protocolConfig.serverConfig.isThirdPartyConfig = true;
        config.protocolConfig = protocolConfig;
        return config;
    }

    ContainerConfig makeSftpConfig()
    {
        ContainerConfig config;
        config.container = DockerContainer::Sftp;

        SftpProtocolConfig protocolConfig;
        protocolConfig.port = QStringLiteral("222");
        protocolConfig.userName = QStringLiteral("sftp_user");
        config.protocolConfig = protocolConfig;
        return config;
    }
} // namespace

class ContainersModelTest : public QObject
{
    Q_OBJECT

private slots:
    void rowsAndRoles();
    void supportAndInstallationPolicy();
    void installedStateAndResetSignal();
    void processedSelection();
};

void ContainersModelTest::rowsAndRoles()
{
    InspectableContainersModel model;
    // Qt 6.10's tester cannot construct the enum-valued DockerContainerRole
    // while probing GUI roles. Ignore only that framework diagnostic; the
    // tester remains in QtTest mode so model-contract failures fail the test.
    QTest::ignoreMessage(
            QtWarningMsg,
            QRegularExpression(QStringLiteral("Trying to construct an instance of an invalid type, type id: \\d+")));
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest, &model);
    Q_UNUSED(tester)

    const auto containers = ContainerUtils::allContainers();
    QCOMPARE(model.rowCount(), containers.size());

    const QHash<int, QByteArray> expectedRoles {
        { ContainersModel::NameRole, "name" },
        { ContainersModel::DescriptionRole, "description" },
        { ContainersModel::DetailedDescriptionRole, "detailedDescription" },
        { ContainersModel::ServiceTypeRole, "serviceType" },
        { ContainersModel::ConfigRole, "config" },
        { ContainersModel::IsThirdPartyConfigRole, "isThirdPartyConfig" },
        { ContainersModel::DockerContainerRole, "dockerContainer" },
        { ContainersModel::ContainerStringRole, "containerString" },
        { ContainersModel::IsEasySetupContainerRole, "isEasySetupContainer" },
        { ContainersModel::EasySetupHeaderRole, "easySetupHeader" },
        { ContainersModel::EasySetupDescriptionRole, "easySetupDescription" },
        { ContainersModel::EasySetupOrderRole, "easySetupOrder" },
        { ContainersModel::IsInstallationAllowedRole, "isInstallationAllowed" },
        { ContainersModel::IsInstalledRole, "isInstalled" },
        { ContainersModel::IsCurrentlyProcessedRole, "isCurrentlyProcessed" },
        { ContainersModel::IsSupportedRole, "isSupported" },
        { ContainersModel::IsShareableRole, "isShareable" },
        { ContainersModel::IsUnsupportedContainerRole, "isUnsupportedContainer" },
        { ContainersModel::InstallPageOrderRole, "installPageOrder" },
        { ContainersModel::IsVpnContainerRole, "isVpnContainer" },
        { ContainersModel::IsServiceContainerRole, "isServiceContainer" },
        { ContainersModel::IsIpsecRole, "isIpsec" },
        { ContainersModel::IsDnsRole, "isDns" },
        { ContainersModel::IsSftpRole, "isSftp" },
        { ContainersModel::IsTorWebsiteRole, "isTorWebsite" },
        { ContainersModel::IsSocks5ProxyRole, "isSocks5Proxy" },
        { ContainersModel::IsMtProxyRole, "isMtProxy" },
        { ContainersModel::IsTelemtRole, "isTelemt" },
        { ContainersModel::IsAgentWorkloadRole, "isAgentWorkload" },
    };
    QCOMPARE(model.roleNames(), expectedRoles);

    const auto humanNames = ContainerUtils::containerHumanNames();
    const auto descriptions = ContainerUtils::containerDescriptions();
    const auto detailedDescriptions = ContainerUtils::containerDetailedDescriptions();
    QVERIFY(!model.data(QModelIndex(), ContainersModel::NameRole).isValid());
    QVERIFY(!model.data(model.index(model.rowCount(), 0), ContainersModel::NameRole).isValid());
    for (int row = 0; row < containers.size(); ++row) {
        const DockerContainer container = containers.at(row);
        const QModelIndex index = model.index(row, 0);

        QCOMPARE(model.data(index, ContainersModel::DockerContainerRole).toInt(), static_cast<int>(container));
        QCOMPARE(model.data(index, ContainersModel::ContainerStringRole).toString(),
                 ContainerUtils::containerToString(container));
        QCOMPARE(model.data(index, ContainersModel::NameRole).toString(),
                 container == DockerContainer::Awg ? QStringLiteral("AmneziaWG Legacy") : humanNames.value(container));
        QCOMPARE(model.data(index, ContainersModel::DescriptionRole).toString(),
                 container == DockerContainer::Awg ? descriptions.value(DockerContainer::Awg)
                                                   : descriptions.value(container));
        QCOMPARE(model.data(index, ContainersModel::DetailedDescriptionRole).toString(),
                 detailedDescriptions.value(container));
        QCOMPARE(model.data(index, ContainersModel::ServiceTypeRole).toInt(),
                 static_cast<int>(ContainerUtils::containerService(container)));
        QCOMPARE(model.data(index, ContainersModel::InstallPageOrderRole).toInt(),
                 ContainerUtils::installPageOrder(container));
        QVERIFY(!model.data(index, ContainersModel::IsDefaultRole).isValid());
    }
}

void ContainersModelTest::supportAndInstallationPolicy()
{
    ContainersModel model;
    const auto containers = ContainerUtils::allContainers();
    for (int row = 0; row < containers.size(); ++row) {
        const DockerContainer container = containers.at(row);
        const QModelIndex index = model.index(row, 0);
        QCOMPARE(model.data(index, ContainersModel::IsSupportedRole).toBool(),
                 ContainerUtils::isSupportedByCurrentPlatform(container));
        QCOMPARE(model.data(index, ContainersModel::IsShareableRole).toBool(), ContainerUtils::isShareable(container));
        QCOMPARE(model.data(index, ContainersModel::IsUnsupportedContainerRole).toBool(),
                 ContainerUtils::isUnsupportedContainer(container));
        QCOMPARE(model.data(index, ContainersModel::IsInstallationAllowedRole).toBool(),
                 ContainersModel::isInstallationAllowed(container));
        QCOMPARE(model.data(index, ContainersModel::IsVpnContainerRole).toBool(),
                 ContainerUtils::containerService(container) == ServiceType::Vpn);
        QCOMPARE(model.data(index, ContainersModel::IsServiceContainerRole).toBool(),
                 ContainerUtils::containerService(container) == ServiceType::Other);
    }

    QCOMPARE(model.rowCount(), ContainerUtils::allContainers().size());
    const int cloakRow = rowForContainer(model, DockerContainer::Cloak);
    const int shadowSocksRow = rowForContainer(model, DockerContainer::ShadowSocks);
    QVERIFY(cloakRow >= 0);
    QVERIFY(shadowSocksRow >= 0);
    QVERIFY(model.data(model.index(cloakRow, 0), ContainersModel::IsUnsupportedContainerRole).toBool());
    QVERIFY(model.data(model.index(shadowSocksRow, 0), ContainersModel::IsUnsupportedContainerRole).toBool());
    QVERIFY(!ContainersModel::isInstallationAllowed(DockerContainer::Awg));
    QVERIFY(!ContainersModel::isInstallationAllowed(DockerContainer::Cloak));
    QVERIFY(!ContainersModel::isInstallationAllowed(DockerContainer::ShadowSocks));

    for (const DockerContainer container : { DockerContainer::AmgptAuthProxy, DockerContainer::OpenClawCodex }) {
        const int row = rowForContainer(model, container);
        QVERIFY(row >= 0);
        const QModelIndex index = model.index(row, 0);
        QVERIFY(model.data(index, ContainersModel::IsServiceContainerRole).toBool());
        QVERIFY(model.data(index, ContainersModel::IsInstallationAllowedRole).toBool());
        QVERIFY(!model.data(index, ContainersModel::IsShareableRole).toBool());
        QVERIFY(model.data(index, ContainersModel::IsAgentWorkloadRole).toBool());
    }

    const int openVpnRow = rowForContainer(model, DockerContainer::OpenVpn);
    QVERIFY(openVpnRow >= 0);
    QVERIFY(!model.data(model.index(openVpnRow, 0), ContainersModel::IsAgentWorkloadRole).toBool());
}

void ContainersModelTest::installedStateAndResetSignal()
{
    ContainersModel model;
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

    const ContainerConfig awgConfig = makeThirdPartyAwgContainerConfig();
    const ContainerConfig sftpConfig = makeSftpConfig();
    const QMap<DockerContainer, ContainerConfig> installed {
        { DockerContainer::Awg, awgConfig },
        { DockerContainer::Sftp, sftpConfig },
    };
    model.updateModel(installed);

    QCOMPARE(resetSpy.count(), 1);
    const int awgRow = rowForContainer(model, DockerContainer::Awg);
    const int sftpRow = rowForContainer(model, DockerContainer::Sftp);
    const int wireguardRow = rowForContainer(model, DockerContainer::WireGuard);
    QVERIFY(awgRow >= 0);
    QVERIFY(sftpRow >= 0);
    QVERIFY(wireguardRow >= 0);
    QVERIFY(model.data(model.index(awgRow, 0), ContainersModel::IsInstalledRole).toBool());
    QVERIFY(model.data(model.index(awgRow, 0), ContainersModel::IsThirdPartyConfigRole).toBool());
    QVERIFY(model.data(model.index(sftpRow, 0), ContainersModel::IsInstalledRole).toBool());
    QVERIFY(!model.data(model.index(wireguardRow, 0), ContainersModel::IsInstalledRole).toBool());

    const QJsonObject awgJson = model.getContainerConfig(awgRow);
    QCOMPARE(awgJson.value(QStringLiteral("container")).toString(), QStringLiteral("amnezia-awg"));
    QCOMPARE(awgJson.value(QStringLiteral("awg")).toObject().value(QStringLiteral("port")).toString(),
             QStringLiteral("55424"));
    QCOMPARE(model.getContainerConfig(wireguardRow), QJsonObject());
    QVERIFY(model.hasInstalledProtocols());
    QVERIFY(model.hasInstalledServices());
}

void ContainersModelTest::processedSelection()
{
    ContainersModel model;
    const int telemtRow = rowForContainer(model, DockerContainer::Telemt);
    QVERIFY(telemtRow >= 0);
    model.setProcessedContainerIndex(telemtRow);

    for (const DockerContainer container : ContainerUtils::allContainers()) {
        const bool selected = container == DockerContainer::Telemt;
        const int row = rowForContainer(model, container);
        QVERIFY(row >= 0);
        QCOMPARE(model.data(model.index(row, 0), ContainersModel::IsCurrentlyProcessedRole).toBool(), selected);
    }
    QCOMPARE(model.getProcessedContainerName(), ContainerUtils::containerHumanNames().value(DockerContainer::Telemt));

    model.setProcessedContainerIndex(-1);
    QCOMPARE(model.getProcessedContainerName(), QString());
}

QTEST_APPLESS_MAIN(ContainersModelTest)

#include "containers_model_test.moc"
