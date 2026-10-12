#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <memory>

#include "core/controllers/selfhosted/installController.h"
#include "core/repositories/secureServersRepository.h"
#include "core/models/agentWorkloadApply.h"
#include "core/models/protocols/amgptAuthProxyProtocolConfig.h"
#include "core/models/protocols/openClawCodexProtocolConfig.h"
#include "core/utils/selfhosted/sshSession.h"

#include "../public_trust_fixture.h"

using namespace amnezia;

namespace
{
    QString decodedDeploymentCommand(const QString &command)
    {
        const QString prefix = QStringLiteral("payload='");
        const auto end = command.indexOf(QStringLiteral("';"), prefix.size());
        return command.startsWith(prefix) && end >= 0
                ? QString::fromUtf8(QByteArray::fromBase64(command.mid(prefix.size(), end - prefix.size()).toLatin1()))
                : command;
    }
    AgentBackendProfileCatalog testCatalog()
    {
        AgentBackendProfileCatalog catalog;
        catalog.development = withPublicTrust(AgentBackendProfile { "development", "https://agpt-auth-dev.amzsvc.com",
            "https://agpt-router-dev.amzsvc.com/v1", "https://agpt-runtime-dev.amzsvc.com" });
        return catalog;
    }

    struct RunnerState
    {
        QString failMarker;
        ErrorCode failCode = ErrorCode::ServerDockerFailedError;
        ErrorCode connectCode = ErrorCode::NoError;
        int failUploadAt = -1;
        ErrorCode uploadFailCode = ErrorCode::SshScpFailureError;
        QString outputMarker;
        QString stdoutText;
        QString stderrText;
        QList<QString> observationOutputs;
        QString mutationOutput;
        QString cleanupOutput = QStringLiteral("AMNEZIA_AGENT_REMOVE_COMPLETE\n");
        QStringList commands;
        QStringList uploads;
        int connectionCount = 0;
        int uploadCount = 0;
    };

    class RecordingRunner final : public ISshCommandRunner
    {
    public:
        explicit RecordingRunner(std::shared_ptr<RunnerState> state) : state(std::move(state))
        {
        }

        ErrorCode connectToHost(const ServerCredentials &) override
        {
            ++state->connectionCount;
            return state->connectCode;
        }

        void disconnectFromHost() override
        {
        }

        ErrorCode executeCommand(const QString &data, const OutputCallback &cbReadStdOut,
                                 const OutputCallback &cbReadStdErr) override
        {
            state->commands.append(data);
            const auto payload = decodedDeploymentCommand(data);
            libssh::Client callbackClient;
            if (data.contains(QStringLiteral("docker container inspect --format"))
                && !state->observationOutputs.isEmpty() && cbReadStdOut) {
                cbReadStdOut(state->observationOutputs.takeFirst(), callbackClient);
            } else if (payload.contains(QStringLiteral("AMNEZIA_AGENT_REMOVE_COMPLETE")) && cbReadStdOut) {
                cbReadStdOut(state->cleanupOutput, callbackClient);
            } else if (data.contains(QStringLiteral("base64 -d | sudo -n sh")) && !state->mutationOutput.isEmpty()
                       && cbReadStdOut) {
                cbReadStdOut(state->mutationOutput, callbackClient);
            }
            if (!state->outputMarker.isEmpty() && data.contains(state->outputMarker, Qt::CaseInsensitive)) {
                if (!state->stdoutText.isEmpty() && cbReadStdOut) {
                    cbReadStdOut(state->stdoutText, callbackClient);
                }
                if (!state->stderrText.isEmpty() && cbReadStdErr) {
                    cbReadStdErr(state->stderrText, callbackClient);
                }
            }
            if (!state->failMarker.isEmpty()
                && (data.contains(state->failMarker, Qt::CaseInsensitive)
                    || payload.contains(state->failMarker, Qt::CaseInsensitive))) {
                return state->failCode;
            }
            return ErrorCode::NoError;
        }

        ErrorCode scpFileCopy(libssh::ScpOverwriteMode, const QString &, const QString &remotePath, const QString &) override
        {
            state->uploads.append(remotePath);
            ++state->uploadCount;
            if (state->uploadCount == state->failUploadAt) {
                return state->uploadFailCode;
            }
            return ErrorCode::NoError;
        }

        ErrorCode getDecryptedPrivateKey(const ServerCredentials &, QString &, const std::function<QString()> &) override
        {
            return ErrorCode::NoError;
        }

    private:
        std::shared_ptr<RunnerState> state;
    };

    struct SessionRecorder
    {
        std::shared_ptr<RunnerState> state = std::make_shared<RunnerState>();

        InstallController::SshSessionFactory factory()
        {
            return [this] {
                auto commandRunner = std::make_unique<RecordingRunner>(state);
                return std::make_unique<SshSession>(nullptr, std::move(commandRunner));
            };
        }
    };

    ServerCredentials testCredentials()
    {
        // root bypasses the local sudo-detection heuristics; the fake runner
        // still records every command and never opens an SSH connection.
        return { QStringLiteral("192.0.2.1"), QStringLiteral("root"), QStringLiteral("fixture-secret"), 22 };
    }

    int commandContaining(const RunnerState &state, const QString &marker, int from = 0)
    {
        for (int i = from; i < state.commands.size(); ++i) {
            if (state.commands.at(i).contains(marker, Qt::CaseInsensitive)
                || decodedDeploymentCommand(state.commands.at(i)).contains(marker, Qt::CaseInsensitive)) {
                return i;
            }
        }
        return -1;
    }

    ContainerConfig openVpnConfig(InstallController &controller)
    {
        return controller.generateConfig(DockerContainer::OpenVpn, 11940, TransportProto::Tcp);
    }

    QByteArray agentObservation(const AgentWorkloadDeploymentSpec &spec, bool present)
    {
        QJsonObject container;
        QJsonArray owners;
        const bool published = !spec.hostPort.isEmpty();
        if (present) {
            QJsonArray mounts;
            for (const auto &volume : spec.volumes) {
                mounts.append(QJsonObject { { QStringLiteral("type"), QStringLiteral("volume") },
                                            { QStringLiteral("name"), volume.name },
                                            { QStringLiteral("destination"), volume.target },
                                        { QStringLiteral("read_only"), volume.readOnly } });
            }
            QJsonArray tmpfs;
            for (const QString &entry : spec.tmpfs) {
                const qsizetype separator = entry.indexOf(QLatin1Char(':'));
                tmpfs.append(QJsonObject { { QStringLiteral("destination"), entry.left(separator) },
                                           { QStringLiteral("options"), entry.mid(separator + 1) } });
            }
            QJsonObject labels;
            const auto deploymentLabels = spec.deploymentLabels();
            for (auto it = deploymentLabels.cbegin(); it != deploymentLabels.cend(); ++it) {
                labels.insert(it.key(), it.value());
            }
            container = QJsonObject {
                { QStringLiteral("id"), QStringLiteral("container-id") },
                { QStringLiteral("name"), spec.containerName },
                { QStringLiteral("image_reference"), spec.imageReference },
                { QStringLiteral("image_id"), QStringLiteral("sha256:runtime") },
                { QStringLiteral("status"), QStringLiteral("running") },
                { QStringLiteral("running"), true },
                { QStringLiteral("health"), QStringLiteral("healthy") },
                { QStringLiteral("restart_policy"), spec.restartPolicy },
                { QStringLiteral("mounts"), mounts },
                { QStringLiteral("tmpfs"), tmpfs },
                { QStringLiteral("networks"), QJsonArray { spec.networkName } },
                { QStringLiteral("ports"),
                  published ? QJsonArray { QJsonObject { { QStringLiteral("host_ip"), QStringLiteral("0.0.0.0") },
                                                         { QStringLiteral("host_port"), spec.hostPort },
                                                         { QStringLiteral("container_port"), spec.containerPort },
                                                         { QStringLiteral("protocol"), QStringLiteral("tcp") } } }
                            : QJsonArray {} },
                { QStringLiteral("labels"), labels },
                { QStringLiteral("trust_environment"), observedTrustEnvironment(spec) },
            };
            if (published) {
                owners.append(QJsonObject { { QStringLiteral("id"), QStringLiteral("container-id") },
                                            { QStringLiteral("name"), spec.containerName } });
            }
        }
        const QJsonValue containerValue = present ? QJsonValue(container) : QJsonValue(QJsonValue::Null);
        return QJsonDocument(QJsonObject {
                                     { QStringLiteral("schema_version"), 1 },
                                     { QStringLiteral("target"),
                                       QJsonObject { { QStringLiteral("workload"), spec.workload },
                                                     { QStringLiteral("container_name"), spec.containerName },
                                                     { QStringLiteral("host_port"), spec.hostPort } } },
                                     { QStringLiteral("container"), containerValue },
                                     { QStringLiteral("host_port_in_use"), present && published },
                                     { QStringLiteral("port_owners"), owners },
                             })
                .toJson(QJsonDocument::Compact);
    }
}

class InstallControllerTest final : public QObject
{
    Q_OBJECT

private slots:
    void linksReadRequiresOnlyInstalledGatewayAndCredentials();
    void gatewayRemovalDoesNotRequireTrust_data();
    void gatewayRemovalDoesNotRequireTrust();
    void explicitTrustRotationPersistsOnlyAfterVerifiedApply();
    void setupContainerRunsExpectedStagesInOrder();
    void updateSkipsPortCheck();
    void setupContainerStopsAtRepresentativeFailures();
    void serverBusyProbeTransportFailureIsCurrentlyIgnored();
    void bestEffortCleanupAndFirewallDoNotAbortSetup();
    void setupContainerPropagatesConnectionAndScpFailures();
    void setupContainerMapsCommandOutputErrors();
    void conntrackInstallFailureIsBestEffort();
    void installAndUpdatePreserveContainerDataDifferently();
    void updateDockerRequirementMatchesContainerSettings();
    void ssXraySharesXrayConfigGeneration();
    void agentWorkloadInstallUsesTypedPathAndAtomicProfile();
    void workloadEnvironmentPersistsIndependently();
    void localProfileAndUnavailableProduction();
    void agentWorkloadInstallKeepsTheSiblingOutOfItsMutation();
    void loginDependencyAndTransportOutcomes_data();
    void loginDependencyAndTransportOutcomes();
};

void InstallControllerTest::linksReadRequiresOnlyInstalledGatewayAndCredentials()
{
    SessionRecorder recorder;
    int profileReads = 0;
    InstallController controller(nullptr, nullptr, nullptr, recorder.factory(),
            [] { return AgentBackendEnvironment::Development; }, [&] { ++profileReads; return AgentBackendProfileCatalog {}; });
    SelfHostedAdminServerConfig config {};
    const auto credentials = testCredentials();
    config.hostName = credentials.hostName;
    config.userName = credentials.userName;
    config.password = credentials.secretData;
    config.port = credentials.port;
    QCOMPARE(controller.queryAgentWorkloadLinks(config).error, AgentWorkloadLinksError::InvalidTarget);
    QVERIFY(recorder.state->commands.isEmpty());
    config.containers.insert(DockerContainer::AmgptAuthProxy, ContainerConfig { DockerContainer::AmgptAuthProxy, AmgptAuthProxyProtocolConfig {} });
    recorder.state->outputMarker = "workloadctl links --json";
    recorder.state->stdoutText = R"({"schema_version":1,"links":[{"rel":"urn:amnezia-gpt:link-relation:client-launch:v1","type":"text/html","href":"http://localhost:5173/login?iss=https%3A%2F%2Fauth.example&target_resource=%2F%2Fdevices.amnezia-gpt%2Fdevices%2Fdev_test"}]})";
    recorder.state->stdoutText = QString::fromLatin1((recorder.state->stdoutText + "\nAMNEZIA_LINKS_EXIT=0\n").toUtf8().toBase64());
    const auto result = controller.queryAgentWorkloadLinks(config);
    QCOMPARE(result.error, AgentWorkloadLinksError::None);
    QVERIFY(result.webUi);
    QCOMPARE(*result.webUi, QString("http://localhost:5173/login?iss=https%3A%2F%2Fauth.example&target_resource=%2F%2Fdevices.amnezia-gpt%2Fdevices%2Fdev_test"));
    QCOMPARE(profileReads, 0);
    QCOMPARE(recorder.state->commands.size(), 1);
    QCOMPARE(recorder.state->uploadCount, 0);
    recorder.state->stdoutText = R"({"schema_version":1,"links":[]})";
    recorder.state->stdoutText = QString::fromLatin1((recorder.state->stdoutText + "\nAMNEZIA_LINKS_EXIT=0\n").toUtf8().toBase64());
    QVERIFY(!controller.queryAgentWorkloadLinks(config).webUi);
    QCOMPARE(recorder.state->commands.size(), 2);
    config.password.clear();
    QCOMPARE(controller.queryAgentWorkloadLinks(config).error, AgentWorkloadLinksError::InvalidTarget);
    QCOMPARE(recorder.state->commands.size(), 2);
}

void InstallControllerTest::gatewayRemovalDoesNotRequireTrust_data()
{
    QTest::addColumn<bool>("missingTrust");
    QTest::addColumn<int>("outcome");
    QTest::newRow("legacy-without-trust") << true << 0;
    QTest::newRow("invalid-pin") << false << 0;
    QTest::newRow("already-missing") << true << 1;
    QTest::newRow("foreign-owner") << true << 2;
    QTest::newRow("unknown-remove-outcome") << true << 3;
    QTest::newRow("retry-still-present") << true << 4;
    QTest::newRow("volume-cleanup-fails") << true << 5;
    QTest::newRow("cleanup-not-confirmed") << true << 6;
}

void InstallControllerTest::gatewayRemovalDoesNotRequireTrust()
{
    QFETCH(bool, missingTrust);
    QFETCH(int, outcome);
    SecureQSettings settings("ClientGatewayRemoval-" + QUuid::createUuid().toString(), "Isolated", nullptr, false);
    SecureAppSettingsRepository appSettings(&settings);
    SecureServersRepository servers(&settings);
    SessionRecorder recorder;
    InstallController controller(&servers, &appSettings, nullptr, recorder.factory(),
                                 [] { return AgentBackendEnvironment::Development; }, testCatalog);
    auto gateway = controller.generateConfig(DockerContainer::AmgptAuthProxy, 0, TransportProto::Tcp);
    const auto installedSpec = makeAgentWorkloadDeploymentSpec(*gateway.getAmgptAuthProxyProtocolConfig());
    QVERIFY(installedSpec);
    auto *profile = gateway.getAmgptAuthProxyProtocolConfig();
    profile->ingressTrustSha256 = missingTrust ? QString() : QString(64, QLatin1Char('0'));
    if (missingTrust) profile->ingressTrustBundle.clear();
    QVERIFY(!makeAgentWorkloadDeploymentSpec(*profile));
    const auto sibling = controller.generateConfig(DockerContainer::OpenClawCodex, 0, TransportProto::Tcp);
    SelfHostedAdminServerConfig admin {};
    const auto credentials = testCredentials();
    admin.hostName = credentials.hostName;
    admin.userName = credentials.userName;
    admin.password = credentials.secretData;
    admin.port = credentials.port;
    admin.defaultContainer = DockerContainer::AmgptAuthProxy;
    admin.updateContainerConfig(DockerContainer::AmgptAuthProxy, gateway);
    admin.updateContainerConfig(DockerContainer::OpenClawCodex, sibling);
    const auto serverId = servers.addServer("test-server", admin.toJson(), serverConfigUtils::ConfigType::SelfHostedAdmin);
    auto before = QJsonDocument::fromJson(agentObservation(*installedSpec, outcome != 1)).object();
    if (outcome == 2) {
        auto container = before.value("container").toObject();
        auto labels = container.value("labels").toObject();
        labels.insert("org.amnezia.amgpt.deployment.managed-by", "another-app");
        container.insert("labels", labels);
        before.insert("container", container);
    }
    const auto after = agentObservation(*installedSpec, outcome == 4);
    recorder.state->observationOutputs = { QString::fromUtf8(QJsonDocument(before).toJson(QJsonDocument::Compact)),
                                            QString::fromUtf8(after) };
    if (outcome == 3) {
        recorder.state->failMarker = "docker container rm --force";
        recorder.state->failCode = ErrorCode::SshTimeoutError;
    } else if (outcome == 5) {
        recorder.state->failMarker = "docker volume rm";
    } else if (outcome == 6) {
        recorder.state->cleanupOutput.clear();
    }
    const auto result = controller.removeContainer(serverId, DockerContainer::AmgptAuthProxy);
    QCOMPARE(result, outcome < 2 ? ErrorCode::NoError : ErrorCode::ServerCheckFailed);
    QVERIFY(recorder.state->connectionCount > 0);
    const auto stored = servers.selfHostedAdminConfig(serverId);
    QVERIFY(stored);
    QCOMPARE(stored->containers.contains(DockerContainer::AmgptAuthProxy), outcome >= 2);
    QCOMPARE(stored->containerConfig(DockerContainer::OpenClawCodex).toJson(), sibling.toJson());
    if (outcome >= 2) {
        QCOMPARE(stored->containerConfig(DockerContainer::AmgptAuthProxy).toJson(), gateway.toJson());
    } else {
        QCOMPARE(stored->defaultContainer, DockerContainer::OpenClawCodex);
    }
    QCOMPARE(commandContaining(*recorder.state, "AMNEZIA_AGENT_REMOVE_COMPLETE") >= 0, outcome != 2);
    QCOMPARE(commandContaining(*recorder.state, "docker volume rm") >= 0, outcome != 2);
}

void InstallControllerTest::explicitTrustRotationPersistsOnlyAfterVerifiedApply()
{
    SecureQSettings settings("ClientTrustRotation-" + QUuid::createUuid().toString(), "Isolated", nullptr, false);
    SecureAppSettingsRepository appSettings(&settings);
    SecureServersRepository servers(&settings);
    SessionRecorder recorder;
    auto catalog = testCatalog();
    auto environment = AgentBackendEnvironment::Development;
    InstallController controller(&servers, &appSettings, nullptr, recorder.factory(),
                                 [&] { return environment; }, [&] { return catalog; });
    SelfHostedAdminServerConfig admin {};
    const auto credentials = testCredentials();
    admin.hostName = credentials.hostName; admin.userName = credentials.userName;
    admin.password = credentials.secretData; admin.port = credentials.port;
    admin.defaultContainer = DockerContainer::AmgptAuthProxy;
    const auto initial = controller.generateConfig(DockerContainer::AmgptAuthProxy, 0, TransportProto::Tcp);
    admin.updateContainerConfig(DockerContainer::AmgptAuthProxy, initial);
    const auto sibling = controller.generateConfig(DockerContainer::OpenClawCodex, 0, TransportProto::Tcp);
    admin.updateContainerConfig(DockerContainer::OpenClawCodex, sibling);
    const auto serverId = servers.addServer("test-server", admin.toJson(), serverConfigUtils::ConfigType::SelfHostedAdmin);
    const auto installedSpec = makeAgentWorkloadDeploymentSpec(*initial.getAmgptAuthProxyProtocolConfig());
    QVERIFY(installedSpec);
    catalog.development = withPublicTrust(catalog.development, "rotated-signer");
    const auto newConfig = controller.generateConfig(DockerContainer::AmgptAuthProxy, 0, TransportProto::Tcp);
    const auto desired = makeAgentWorkloadDeploymentSpec(*newConfig.getAmgptAuthProxyProtocolConfig());
    QVERIFY(desired);
    // An unconfirmed mutation leaves the saved installation profile untouched.
    recorder.state->observationOutputs = { QString::fromUtf8(agentObservation(*installedSpec, true)),
                                            QString::fromUtf8(agentObservation(*installedSpec, true)) };
    recorder.state->failMarker = "base64 -d | sudo -n sh";
    controller.reconcileAgentWorkload(serverId, DockerContainer::AmgptAuthProxy);
    QCOMPARE(servers.selfHostedAdminConfig(serverId)->containerConfig(DockerContainer::AmgptAuthProxy).toJson(), initial.toJson());
    recorder.state->failMarker.clear();
    recorder.state->observationOutputs = { QString::fromUtf8(agentObservation(*installedSpec, true)),
        QString::fromUtf8(agentObservation(*installedSpec, true)), QString::fromUtf8(agentObservation(*desired, true)) };
    recorder.state->mutationOutput = "AMNEZIA_AGENT_APPLY_APPLIED\n";
    const auto result = controller.reconcileAgentWorkload(serverId, DockerContainer::AmgptAuthProxy);
    QCOMPARE(result.plan.action, AgentWorkloadReconciliationAction::Recreate);
    QCOMPARE(result.applyResult.status, AgentWorkloadApplyStatus::Applied);
    const auto stored = servers.selfHostedAdminConfig(serverId);
    QCOMPARE(stored->containerConfig(DockerContainer::AmgptAuthProxy).toJson(), newConfig.toJson());
    QCOMPARE(stored->containerConfig(DockerContainer::OpenClawCodex).toJson(), sibling.toJson());
    QCOMPARE(installedSpec->volumes.first().name, desired->volumes.first().name);
    const auto connections = recorder.state->connectionCount;
    environment = AgentBackendEnvironment::Local;
    catalog.local = withPublicTrust(AgentBackendProfile { "local", "https://local.example", "https://router.local.example/v1", "https://runtime.local.example" });
    QCOMPARE(controller.reconcileAgentWorkload(serverId, DockerContainer::AmgptAuthProxy).validationError,
             AgentDeploymentValidationError::BackendEnvironmentChanged);
    environment = AgentBackendEnvironment::Development;
    catalog.development.authIssuer = "https://changed-issuer.example";
    catalog.development = withPublicTrust(catalog.development);
    QCOMPARE(controller.reconcileAgentWorkload(serverId, DockerContainer::AmgptAuthProxy).validationError,
             AgentDeploymentValidationError::BackendEnvironmentChanged);
    QCOMPARE(recorder.state->connectionCount, connections);
    settings.clearSettings();
}

void InstallControllerTest::workloadEnvironmentPersistsIndependently()
{
    const auto organization = QStringLiteral("AgentEnvironmentTest-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        SecureQSettings settings(organization, "Isolated", nullptr, false);
        SecureAppSettingsRepository repository(&settings);
        QCOMPARE(repository.agentWorkloadEnvironment(), QStringLiteral("dev"));
        repository.toggleDevGatewayEnv(true);
        QCOMPARE(repository.agentWorkloadEnvironment(), QStringLiteral("dev"));
        repository.setAgentWorkloadEnvironment("local");
        repository.toggleDevGatewayEnv(false);
        QCOMPARE(repository.agentWorkloadEnvironment(), QStringLiteral("local"));
        repository.setAgentWorkloadEnvironment("prod");
        QCOMPARE(repository.agentWorkloadEnvironment(), QStringLiteral("local"));
        SessionRecorder recorder;
        InstallController controller(nullptr, &repository, nullptr, recorder.factory());
        const auto config = controller.generateConfig(DockerContainer::AmgptAuthProxy, 8080, TransportProto::Tcp);
        QVERIFY(!makeAgentWorkloadDeploymentSpec(*config.getAmgptAuthProxyProtocolConfig()));
        QVERIFY(repository.localAgentBackendProfile().isEmpty());
        QVERIFY(!repository.saveLocalAgentBackendProfile("https://auth.example", "", "https://runtime.example", publicTrustFixture("https://auth.example"), trustPin(publicTrustFixture("https://auth.example"))));
        QVERIFY(repository.saveLocalAgentBackendProfile("https://auth.example", "https://router.example/v1",
                                                       "https://runtime.example", publicTrustFixture("https://auth.example"), trustPin(publicTrustFixture("https://auth.example"))));
        const auto profile = repository.localAgentBackendProfile();
        QVERIFY(!repository.saveLocalAgentBackendProfile("https://auth.example", "https://router.example/v1", "", publicTrustFixture("https://auth.example"), trustPin(publicTrustFixture("https://auth.example"))));
        QVERIFY(!repository.saveLocalAgentBackendProfile("https://auth.example", "https://router.example/v1",
                                                        "https://runtime.example/v1", publicTrustFixture("https://auth.example"), trustPin(publicTrustFixture("https://auth.example"))));
        QVERIFY(!repository.saveLocalAgentBackendProfile("http://auth.example", "https://router.example/v1",
                                                       "https://runtime.example", publicTrustFixture("https://auth.example"), trustPin(publicTrustFixture("https://auth.example"))));
        QVERIFY(!repository.saveLocalAgentBackendProfile("https://auth.example", "https://router.example/v1",
                "https://runtime.example", publicTrustFixture("https://other.example"), trustPin(publicTrustFixture("https://other.example"))));
        QVERIFY(!repository.saveLocalAgentBackendProfile("https://auth.example", "https://router.example/v1",
                "https://runtime.example", publicTrustFixture("https://auth.example"), QString(64, '0')));
        QVERIFY(!repository.saveLocalAgentBackendProfile("https://auth.example", "https://router.example/v1",
                "https://runtime.example", "broken", trustPin("broken")));
        QCOMPARE(repository.localAgentBackendProfile(), profile);
        const auto configured = controller.generateConfig(DockerContainer::AmgptAuthProxy, 8080, TransportProto::Tcp);
        QCOMPARE(configured.getAmgptAuthProxyProtocolConfig()->authIssuer, QStringLiteral("https://auth.example"));
        QCOMPARE(configured.getAmgptAuthProxyProtocolConfig()->routerBaseUrl, QStringLiteral("https://router.example/v1"));
        QCOMPARE(configured.getAmgptAuthProxyProtocolConfig()->runtimeGatewayBaseUrl,
                 QStringLiteral("https://runtime.example"));
        QVERIFY(repository.backupAppConfig().contains("Conf/agentWorkloadEnvironment"));
    }
    {
        SecureQSettings settings(organization, "Isolated", nullptr, false);
        SecureAppSettingsRepository repository(&settings);
        QCOMPARE(repository.agentWorkloadEnvironment(), QStringLiteral("local"));
        QCOMPARE(repository.localAgentBackendProfile().value("authIssuer").toString(), QStringLiteral("https://auth.example"));
        QCOMPARE(repository.localAgentBackendProfile().value("runtimeGatewayBaseUrl").toString(),
                 QStringLiteral("https://runtime.example"));
        QCOMPARE(repository.localAgentBackendProfile().value("ingressTrustBundle").toString(), publicTrustFixture("https://auth.example"));
        QCOMPARE(repository.localAgentBackendProfile().value("ingressTrustSha256").toString(), trustPin(publicTrustFixture("https://auth.example")));
        repository.setAgentWorkloadEnvironment("dev");
        QCOMPARE(repository.agentWorkloadEnvironment(), QStringLiteral("dev"));
        settings.setValue("Conf/agentWorkloadEnvironment", "invalid-import");
        SessionRecorder recorder;
        InstallController controller(nullptr, &repository, nullptr, recorder.factory());
        const auto config = controller.generateConfig(DockerContainer::AmgptAuthProxy, 8080, TransportProto::Tcp);
        QVERIFY(!makeAgentWorkloadDeploymentSpec(*config.getAmgptAuthProxyProtocolConfig()));
        QCOMPARE(recorder.state->commands.size(), 0);
        settings.clearSettings();
    }
}

void InstallControllerTest::localProfileAndUnavailableProduction()
{
    SessionRecorder recorder;
    auto environment = AgentBackendEnvironment::Local;
    InstallController controller(nullptr, nullptr, nullptr, recorder.factory(), [&] { return environment; }, testCatalog);
    const auto config = controller.generateConfig(DockerContainer::AmgptAuthProxy, 8080, TransportProto::Tcp);
    const auto *proxy = config.getAmgptAuthProxyProtocolConfig();
    QVERIFY(proxy);
    QVERIFY(!makeAgentWorkloadDeploymentSpec(*proxy));
    ContainerConfig attempted;
    QVERIFY(controller.installContainer(testCredentials(), DockerContainer::AmgptAuthProxy, 0, TransportProto::Tcp,
                                        attempted) != ErrorCode::NoError);
    QCOMPARE(recorder.state->connectionCount, 0);
    environment = AgentBackendEnvironment::Production;
    const auto unavailable = controller.generateConfig(DockerContainer::AmgptAuthProxy, 8080, TransportProto::Tcp);
    QVERIFY(!makeAgentWorkloadDeploymentSpec(*unavailable.getAmgptAuthProxyProtocolConfig()));
}

void InstallControllerTest::loginDependencyAndTransportOutcomes_data()
{
    QTest::addColumn<QString>("proxyState");
    QTest::addColumn<bool>("includeOpenClaw");
    QTest::addColumn<int>("expectedPrecondition");
    QTest::addColumn<QString>("commandFailure");
    using P = AgentWorkloadLoginPrecondition;
    QTest::newRow("amgpt-healthy") << QString("healthy") << true << int(P::None) << QString();
    QTest::newRow("amgpt-does-not-require-openclaw") << QString("healthy") << false << int(P::None) << QString();
    QTest::newRow("amgpt-missing") << QString("missing") << true << int(P::DeviceGatewayMissing) << QString();
    for (const QString &state : { QString("stopped"), QString("unhealthy"), QString("drift"), QString("conflict"),
                                 QString("unknown") }) {
        QTest::newRow(qPrintable(state)) << state << true << int(P::DeviceGatewayNotReady) << QString();
    }
    QTest::newRow("login-transport-failure") << QString("healthy") << true << int(P::None) << QString("transport");
    QTest::newRow("login-invalid-response") << QString("healthy") << true << int(P::None) << QString("invalid");
}

void InstallControllerTest::loginDependencyAndTransportOutcomes()
{
    QFETCH(QString, proxyState);
    QFETCH(bool, includeOpenClaw);
    QFETCH(int, expectedPrecondition);
    QFETCH(QString, commandFailure);
    SessionRecorder recorder;
    auto selectedEnvironment = AgentBackendEnvironment::Development;
    InstallController controller(nullptr, nullptr, nullptr, recorder.factory(),
                                 [&] { return selectedEnvironment; }, testCatalog);
    SelfHostedAdminServerConfig config {};
    const auto credentials = testCredentials();
    config.hostName = credentials.hostName;
    config.userName = credentials.userName;
    config.password = credentials.secretData;
    config.port = credentials.port;
    if (includeOpenClaw) {
        config.containers.insert(DockerContainer::OpenClawCodex,
                                 controller.generateConfig(DockerContainer::OpenClawCodex, 28789,
                                                           TransportProto::Tcp));
    }
    QString proxyObservation;
    if (proxyState != "missing") {
        config.containers.insert(DockerContainer::AmgptAuthProxy,
                                 controller.generateConfig(DockerContainer::AmgptAuthProxy, 18080, TransportProto::Tcp));
        const auto proxySpec = makeAgentWorkloadDeploymentSpec(
                *config.containers[DockerContainer::AmgptAuthProxy].getAmgptAuthProxyProtocolConfig());
        QVERIFY(proxySpec);
        auto observation = QJsonDocument::fromJson(agentObservation(*proxySpec, true)).object();
        auto container = observation["container"].toObject();
        if (proxyState == "stopped") {
            container["running"] = false;
            container["status"] = "exited";
        } else if (proxyState == "unhealthy") {
            container["health"] = "unhealthy";
        } else if (proxyState == "drift") {
            container["image_reference"] = "example.invalid/previous:v1";
        } else if (proxyState == "conflict") {
            container["labels"] = QJsonObject();
        }
        observation["container"] = container;
        proxyObservation = proxyState == "unknown" ? QString("not-json")
                                                     : QString::fromUtf8(
                                                             QJsonDocument(observation).toJson(QJsonDocument::Compact));
    }
    // Installed configuration must remain authoritative even when the global
    // selection becomes unavailable (or changes to another environment).
    selectedEnvironment = static_cast<AgentBackendEnvironment>(99);
    if (proxyState != "missing") {
        recorder.state->observationOutputs.append(proxyObservation);
        recorder.state->observationOutputs.append(proxyObservation);
    }
    recorder.state->outputMarker = "workloadctl login start";
    const auto mode = AgentWorkloadLoginMode::Amgpt;
    recorder.state->stdoutText = QString::fromUtf8(QJsonDocument(QJsonObject {
            { "schema_version", 1 }, { "mode", agentWorkloadLoginModeName(mode) }, { "state", "pending" },
            { "verification_uri", "https://auth.example/device" }, { "verification_uri_complete", QJsonValue::Null },
            { "user_code", "ABCD-EFGH" }, { "expires_in", QJsonValue::Null }, { "error_code", QJsonValue::Null }
    }).toJson(QJsonDocument::Compact));
    if (commandFailure == "transport") recorder.state->failMarker = recorder.state->outputMarker;
    if (commandFailure == "invalid") recorder.state->stdoutText = "not-json";

    const auto result = controller.startAgentWorkloadLogin(config, mode);

    QCOMPARE(int(result.precondition), expectedPrecondition);
    if (expectedPrecondition == int(AgentWorkloadLoginPrecondition::None)) {
        const auto expectedError = commandFailure == "transport" ? AgentWorkloadLoginOperationError::CommandFailed
                : commandFailure == "invalid" ? AgentWorkloadLoginOperationError::InvalidResponse
                                               : AgentWorkloadLoginOperationError::None;
        QCOMPARE(result.startResult.error, expectedError);
        QCOMPARE(result.startResult.presentation.has_value(), commandFailure.isEmpty());
    } else {
        QCOMPARE(commandContaining(*recorder.state, "workloadctl login start"), -1);
    }
    QCOMPARE(commandContaining(*recorder.state, "base64 -d | sudo -n sh"), -1);
    QCOMPARE(commandContaining(*recorder.state, "docker run"), -1);
    QCOMPARE(recorder.state->uploadCount, 0);
    if (expectedPrecondition == int(AgentWorkloadLoginPrecondition::None)) {
        QVERIFY(commandContaining(*recorder.state, "docker exec -i amnezia-amgpt-device-gateway workloadctl") >= 0);
        QCOMPARE(commandContaining(*recorder.state, "docker exec -i amnezia-openclaw-codex workloadctl"), -1);
    }
}

void InstallControllerTest::agentWorkloadInstallUsesTypedPathAndAtomicProfile()
{
    SessionRecorder recorder;
    InstallController controller(nullptr, nullptr, nullptr, recorder.factory(),
                                 [] { return AgentBackendEnvironment::Development; }, testCatalog);
    ContainerConfig config;

    AmgptAuthProxyProtocolConfig expectedConfig;
    expectedConfig.backendProfile = QStringLiteral("development");
    expectedConfig.authIssuer = QStringLiteral("https://agpt-auth-dev.amzsvc.com");
    expectedConfig.routerBaseUrl = QStringLiteral("https://agpt-router-dev.amzsvc.com/v1");
    expectedConfig.runtimeGatewayBaseUrl = QStringLiteral("https://agpt-runtime-dev.amzsvc.com");
    const auto expectedSpec = makeAgentWorkloadDeploymentSpec(withPublicTrust(expectedConfig));
    QVERIFY(expectedSpec);
    recorder.state->observationOutputs = {
        QString::fromUtf8(agentObservation(*expectedSpec, false)),
        QString::fromUtf8(agentObservation(*expectedSpec, false)),
        QString::fromUtf8(agentObservation(*expectedSpec, true)),
    };
    recorder.state->mutationOutput = QStringLiteral("AMNEZIA_AGENT_APPLY_APPLIED\n");

    QCOMPARE(controller.installContainer(testCredentials(), DockerContainer::AmgptAuthProxy, 18080, TransportProto::Tcp,
                                         config),
             ErrorCode::NoError);

    const auto *proxy = config.getAmgptAuthProxyProtocolConfig();
    QVERIFY(proxy);
    QVERIFY(proxy->port.isEmpty());
    QCOMPARE(proxy->backendProfile, QStringLiteral("development"));
    QCOMPARE(proxy->authIssuer, QStringLiteral("https://agpt-auth-dev.amzsvc.com"));
    QCOMPARE(proxy->routerBaseUrl, QStringLiteral("https://agpt-router-dev.amzsvc.com/v1"));
    QCOMPARE(proxy->runtimeGatewayBaseUrl, QStringLiteral("https://agpt-runtime-dev.amzsvc.com"));
    QVERIFY(commandContaining(*recorder.state, QStringLiteral("base64 -d | sudo -n sh")) >= 0);
    QCOMPARE(commandContaining(*recorder.state, QStringLiteral("docker build")), -1);
}

void InstallControllerTest::agentWorkloadInstallKeepsTheSiblingOutOfItsMutation()
{
    SessionRecorder recorder;
    InstallController controller(nullptr, nullptr, nullptr, recorder.factory(), {}, testCatalog);
    ContainerConfig config;
    OpenClawCodexProtocolConfig expectedConfig;
    const auto expectedSpec = makeAgentWorkloadDeploymentSpec(expectedConfig);
    QVERIFY(expectedSpec);
    recorder.state->observationOutputs = {
        QString::fromUtf8(agentObservation(*expectedSpec, false)),
        QString::fromUtf8(agentObservation(*expectedSpec, false)),
        QString::fromUtf8(agentObservation(*expectedSpec, true)),
    };
    recorder.state->mutationOutput = QStringLiteral("AMNEZIA_AGENT_APPLY_APPLIED\n");

    QCOMPARE(controller.installContainer(testCredentials(), DockerContainer::OpenClawCodex, 28789, TransportProto::Tcp,
                                         config),
             ErrorCode::NoError);

    const int mutation = commandContaining(*recorder.state, QStringLiteral("base64 -d | sudo -n sh"));
    QVERIFY(mutation >= 0);
    QVERIFY(!recorder.state->commands.at(mutation).contains(QStringLiteral("amnezia-amgpt-device-gateway")));
    QCOMPARE(commandContaining(*recorder.state, QStringLiteral("docker build")), -1);
}

void InstallControllerTest::setupContainerRunsExpectedStagesInOrder()
{
    const QList<QPair<bool, QStringList>> cases = {
        { false,
          { QStringLiteral("sudo -K"), QStringLiteral("LOCK_FILE"), QStringLiteral("echo \"Dist:"),
            QStringLiteral("lsof -i -P -n"), QStringLiteral("chown"), QStringLiteral("docker stop"),
            QStringLiteral("rm /opt/amnezia/amnezia-openvpn/Dockerfile"), QStringLiteral("docker build"),
            QStringLiteral("docker run"), QStringLiteral("docker cp"), QStringLiteral("bash /opt/amnezia/"),
            QStringLiteral("sysctl -w net.ipv4.ip_forward"), QStringLiteral("chmod a+x /opt/amnezia/start.sh") } },
        { true,
          { QStringLiteral("sudo -K"), QStringLiteral("LOCK_FILE"), QStringLiteral("echo \"Dist:"),
            QStringLiteral("chown"), QStringLiteral("docker stop"),
            QStringLiteral("rm /opt/amnezia/amnezia-openvpn/Dockerfile"), QStringLiteral("docker build"),
            QStringLiteral("docker run"), QStringLiteral("docker cp"), QStringLiteral("bash /opt/amnezia/"),
            QStringLiteral("sysctl -w net.ipv4.ip_forward"), QStringLiteral("chmod a+x /opt/amnezia/start.sh") } }
    };

    for (const auto &[isUpdate, stages] : cases) {
        SessionRecorder recorder;
        InstallController controller(nullptr, nullptr, nullptr, recorder.factory(), {}, testCatalog);
        ContainerConfig config = openVpnConfig(controller);

        QCOMPARE(controller.setupContainer(testCredentials(), DockerContainer::OpenVpn, config, isUpdate),
                 ErrorCode::NoError);
        QVERIFY(recorder.state->connectionCount > 0);
        QVERIFY(!recorder.state->commands.isEmpty());

        int previous = -1;
        for (const QString &stage : stages) {
            const int current = commandContaining(*recorder.state, stage, previous + 1);
            QVERIFY2(current > previous, qPrintable(QStringLiteral("stage missing or out of order: %1").arg(stage)));
            previous = current;
        }
        if (isUpdate) {
            QCOMPARE(commandContaining(*recorder.state, QStringLiteral("lsof -i -P -n")), -1);
        }
        QVERIFY(!recorder.state->uploads.isEmpty());
    }
}

void InstallControllerTest::updateSkipsPortCheck()
{
    SessionRecorder recorder;
    InstallController controller(nullptr, nullptr, nullptr, recorder.factory(), {}, testCatalog);
    ContainerConfig config = openVpnConfig(controller);

    QCOMPARE(controller.setupContainer(testCredentials(), DockerContainer::OpenVpn, config, true), ErrorCode::NoError);
    QVERIFY(recorder.state->connectionCount > 0);
    QCOMPARE(commandContaining(*recorder.state, QStringLiteral("lsof -i -P -n")), -1);
    QVERIFY(commandContaining(*recorder.state, QStringLiteral("docker build")) >= 0);
}

void InstallControllerTest::setupContainerStopsAtRepresentativeFailures()
{
    struct FatalStage
    {
        QString marker;
        QString laterStage;
        ErrorCode expected;
    };
    const QList<FatalStage> stages = {
        { QStringLiteral("sudo -K"), QStringLiteral("LOCK_FILE"), ErrorCode::SshRequestDeniedError },
        { QStringLiteral("echo \"Dist:"), QStringLiteral("lsof -i -P -n"), ErrorCode::SshRequestDeniedError },
        { QStringLiteral("lsof -i -P -n"), QStringLiteral("chown"), ErrorCode::SshRequestDeniedError },
        { QStringLiteral("chown"), QStringLiteral("rm /opt/amnezia/amnezia-openvpn/Dockerfile"),
          ErrorCode::SshRequestDeniedError },
        { QStringLiteral("rm /opt/amnezia/amnezia-openvpn/Dockerfile"), QStringLiteral("docker build"),
          ErrorCode::SshRequestDeniedError },
        { QStringLiteral("docker build"), QStringLiteral("docker run"), ErrorCode::SshRequestDeniedError },
        { QStringLiteral("docker run"), QStringLiteral("docker cp"), ErrorCode::SshRequestDeniedError },
        { QStringLiteral("docker cp"), QStringLiteral("bash /opt/amnezia/"), ErrorCode::SshRequestDeniedError },
        { QStringLiteral("bash /opt/amnezia/"), QStringLiteral("sysctl -w net.ipv4.ip_forward"),
          ErrorCode::SshRequestDeniedError },
        { QStringLiteral("chmod a+x /opt/amnezia/start.sh"), QStringLiteral("never-issued"),
          ErrorCode::SshRequestDeniedError }
    };

    for (const FatalStage &stage : stages) {
        SessionRecorder recorder;
        recorder.state->failMarker = stage.marker;
        recorder.state->failCode = stage.expected;
        InstallController controller(nullptr, nullptr, nullptr, recorder.factory(), {}, testCatalog);
        ContainerConfig config = openVpnConfig(controller);

        const ErrorCode actual = controller.setupContainer(testCredentials(), DockerContainer::OpenVpn, config, false);
        QVERIFY2(actual == stage.expected,
                 qPrintable(QStringLiteral("stage did not propagate its injected failure: %1").arg(stage.marker)));

        const int failed = commandContaining(*recorder.state, stage.marker);
        QVERIFY(failed >= 0);
        QCOMPARE(commandContaining(*recorder.state, stage.laterStage, failed + 1), -1);
        if (stage.marker != QStringLiteral("chmod a+x /opt/amnezia/start.sh")) {
            QCOMPARE(commandContaining(*recorder.state, QStringLiteral("chmod a+x /opt/amnezia/start.sh"), failed + 1),
                     -1);
        }
    }
}

void InstallControllerTest::serverBusyProbeTransportFailureIsCurrentlyIgnored()
{
    SessionRecorder recorder;
    recorder.state->failMarker = QStringLiteral("LOCK_FILE");
    recorder.state->failCode = ErrorCode::SshRequestDeniedError;
    InstallController controller(nullptr, nullptr, nullptr, recorder.factory(), {}, testCatalog);
    ContainerConfig config = openVpnConfig(controller);

    // isServerDpkgBusy currently discards the SshSession error and decides
    // only from captured output. Characterize that behavior without treating
    // it as an intentional best-effort stage; production error propagation is
    // a separate behavioral fix.
    QCOMPARE(controller.setupContainer(testCredentials(), DockerContainer::OpenVpn, config, false), ErrorCode::NoError);
    QVERIFY(commandContaining(*recorder.state, QStringLiteral("LOCK_FILE")) >= 0);
    QVERIFY(commandContaining(*recorder.state, QStringLiteral("docker build")) >= 0);
}

void InstallControllerTest::bestEffortCleanupAndFirewallDoNotAbortSetup()
{
    const QStringList ignoredStages = { QStringLiteral("docker stop"), QStringLiteral("ip_forward"),
                                        QStringLiteral("docker exec -i amnezia-openvpn rm /opt/amnezia/"),
                                        QStringLiteral("shred -u") };
    for (const QString &ignoredStage : ignoredStages) {
        SessionRecorder recorder;
        recorder.state->failMarker = ignoredStage;
        InstallController controller(nullptr, nullptr, nullptr, recorder.factory(), {}, testCatalog);
        ContainerConfig config = openVpnConfig(controller);

        QCOMPARE(controller.setupContainer(testCredentials(), DockerContainer::OpenVpn, config, false),
                 ErrorCode::NoError);
        QVERIFY(commandContaining(*recorder.state, QStringLiteral("docker build")) >= 0);
        QVERIFY(commandContaining(*recorder.state, QStringLiteral("chmod a+x /opt/amnezia/start.sh")) >= 0);
    }
}

void InstallControllerTest::ssXraySharesXrayConfigGeneration()
{
    InstallController controller(nullptr, nullptr);
    const ContainerConfig config = controller.generateConfig(DockerContainer::SSXray, 443, TransportProto::Tcp);

    QCOMPARE(config.container, DockerContainer::SSXray);
    QVERIFY(config.getXrayProtocolConfig() != nullptr);
}

void InstallControllerTest::setupContainerPropagatesConnectionAndScpFailures()
{
    {
        SessionRecorder recorder;
        recorder.state->connectCode = ErrorCode::SshTimeoutError;
        InstallController controller(nullptr, nullptr, nullptr, recorder.factory(), {}, testCatalog);
        ContainerConfig config = openVpnConfig(controller);

        QCOMPARE(controller.setupContainer(testCredentials(), DockerContainer::OpenVpn, config, false),
                 ErrorCode::SshTimeoutError);
        QCOMPARE(recorder.state->commands.size(), 0);
    }

    {
        SessionRecorder recorder;
        recorder.state->failUploadAt = 1;
        recorder.state->uploadFailCode = ErrorCode::SshScpFailureError;
        InstallController controller(nullptr, nullptr, nullptr, recorder.factory(), {}, testCatalog);
        ContainerConfig config = openVpnConfig(controller);

        QCOMPARE(controller.setupContainer(testCredentials(), DockerContainer::OpenVpn, config, false),
                 ErrorCode::SshScpFailureError);
        QVERIFY(commandContaining(*recorder.state, QStringLiteral("docker build")) == -1);
        QVERIFY(commandContaining(*recorder.state, QStringLiteral("docker run")) == -1);
    }

    {
        SessionRecorder recorder;
        recorder.state->failUploadAt = 2;
        recorder.state->uploadFailCode = ErrorCode::SshScpFailureError;
        InstallController controller(nullptr, nullptr, nullptr, recorder.factory(), {}, testCatalog);
        ContainerConfig config = openVpnConfig(controller);

        QCOMPARE(controller.setupContainer(testCredentials(), DockerContainer::OpenVpn, config, false),
                 ErrorCode::SshScpFailureError);
        QVERIFY(commandContaining(*recorder.state, QStringLiteral("docker cp")) == -1);
        QVERIFY(commandContaining(*recorder.state, QStringLiteral("bash /opt/amnezia/")) == -1);
    }

    {
        SessionRecorder recorder;
        recorder.state->failUploadAt = 3;
        recorder.state->uploadFailCode = ErrorCode::SshScpFailureError;
        InstallController controller(nullptr, nullptr, nullptr, recorder.factory(), {}, testCatalog);
        ContainerConfig config = openVpnConfig(controller);

        QCOMPARE(controller.setupContainer(testCredentials(), DockerContainer::OpenVpn, config, false),
                 ErrorCode::SshScpFailureError);
        QVERIFY(commandContaining(*recorder.state, QStringLiteral("chmod a+x /opt/amnezia/start.sh")) == -1);
    }
}

void InstallControllerTest::setupContainerMapsCommandOutputErrors()
{
    struct OutputCase
    {
        QString marker;
        QString output;
        ErrorCode expected;
        QString laterStage;
    };
    const QList<OutputCase> cases = { { QStringLiteral("echo \"Dist:"), QStringLiteral("lock"),
                                        ErrorCode::ServerPacketManagerError, QStringLiteral("lsof -i -P -n") },
                                      { QStringLiteral("lsof -i -P -n"), QStringLiteral("11940 LISTEN"),
                                        ErrorCode::ServerPortAlreadyAllocatedError, QStringLiteral("docker build") },
                                      { QStringLiteral("docker build"), QStringLiteral("failed to solve: fixture"),
                                        ErrorCode::ServerDockerFailedError, QStringLiteral("docker run") },
                                      { QStringLiteral("docker run"), QStringLiteral("address already in use"),
                                        ErrorCode::ServerPortAlreadyAllocatedError, QStringLiteral("docker cp") } };

    for (const OutputCase &testCase : cases) {
        SessionRecorder recorder;
        recorder.state->outputMarker = testCase.marker;
        recorder.state->stdoutText = testCase.output;
        InstallController controller(nullptr, nullptr, nullptr, recorder.factory(), {}, testCatalog);
        ContainerConfig config = openVpnConfig(controller);

        QCOMPARE(controller.setupContainer(testCredentials(), DockerContainer::OpenVpn, config, false),
                 testCase.expected);
        const int outputStage = commandContaining(*recorder.state, testCase.marker);
        QVERIFY(outputStage >= 0);
        QCOMPARE(commandContaining(*recorder.state, testCase.laterStage, outputStage + 1), -1);
    }
}

void InstallControllerTest::conntrackInstallFailureIsBestEffort()
{
    SessionRecorder recorder;
    recorder.state->failMarker = QStringLiteral("conntrack");
    recorder.state->failCode = ErrorCode::SshRequestDeniedError;
    InstallController controller(nullptr, nullptr, nullptr, recorder.factory(), {}, testCatalog);
    ContainerConfig config = controller.generateConfig(DockerContainer::MtProxy, 8443, TransportProto::Tcp);

    QCOMPARE(controller.setupContainer(testCredentials(), DockerContainer::MtProxy, config, false), ErrorCode::NoError);
    const int conntrack = commandContaining(*recorder.state, QStringLiteral("conntrack"));
    QVERIFY(conntrack >= 0);
    QVERIFY(commandContaining(*recorder.state, QStringLiteral("docker build"), conntrack + 1) >= 0);
    QVERIFY(commandContaining(*recorder.state, QStringLiteral("chmod a+x /opt/amnezia/start.sh")) >= 0);
}

void InstallControllerTest::installAndUpdatePreserveContainerDataDifferently()
{
    const QList<QPair<DockerContainer, QString>> cases = {
        { DockerContainer::MtProxy, QStringLiteral("docker volume rm -f amnezia-mtproxy-data") },
        { DockerContainer::Telemt, QStringLiteral("docker volume rm -f amnezia-telemt-data") }
    };

    for (const auto &[container, volumeCommand] : cases) {
        SessionRecorder installRecorder;
        InstallController installController(nullptr, nullptr, nullptr, installRecorder.factory());
        ContainerConfig installConfig = installController.generateConfig(container, 8443, TransportProto::Tcp);
        QCOMPARE(installController.setupContainer(testCredentials(), container, installConfig, false),
                 ErrorCode::NoError);
        QVERIFY(commandContaining(*installRecorder.state, volumeCommand) >= 0);

        SessionRecorder updateRecorder;
        InstallController updateController(nullptr, nullptr, nullptr, updateRecorder.factory());
        ContainerConfig updateConfig = updateController.generateConfig(container, 8443, TransportProto::Tcp);
        QCOMPARE(updateController.setupContainer(testCredentials(), container, updateConfig, true), ErrorCode::NoError);
        QCOMPARE(commandContaining(*updateRecorder.state, volumeCommand), -1);
    }
}

void InstallControllerTest::updateDockerRequirementMatchesContainerSettings()
{
    InstallController controller(nullptr, nullptr);

    struct RequirementCase
    {
        DockerContainer container;
        bool equalSettingsRequireUpdate;
    };
    const QList<RequirementCase> cases = { { DockerContainer::Awg, false },       { DockerContainer::Awg2, false },
                                           { DockerContainer::WireGuard, false }, { DockerContainer::MtProxy, false },
                                           { DockerContainer::Telemt, false },    { DockerContainer::OpenVpn, true },
                                           { DockerContainer::Xray, true },       { DockerContainer::SSXray, true },
                                           { DockerContainer::Socks5Proxy, true } };

    for (const RequirementCase &testCase : cases) {
        ContainerConfig oldConfig = controller.generateConfig(testCase.container, 11940, TransportProto::Tcp);
        ContainerConfig sameConfig = oldConfig;
        ContainerConfig changedConfig = oldConfig;

        if (auto *awg = changedConfig.getAwgProtocolConfig()) {
            awg->serverConfig.port = QStringLiteral("11941");
        } else if (auto *wireguard = changedConfig.getWireGuardProtocolConfig()) {
            wireguard->serverConfig.port = QStringLiteral("11941");
        } else if (auto *mtProxy = changedConfig.getMtProxyProtocolConfig()) {
            mtProxy->port = QStringLiteral("11941");
        } else if (auto *telemt = changedConfig.getTelemtProtocolConfig()) {
            telemt->port = QStringLiteral("11941");
        }

        QCOMPARE(controller.isUpdateDockerContainerRequired(testCase.container, oldConfig, sameConfig),
                 testCase.equalSettingsRequireUpdate);
        QCOMPARE(controller.isUpdateDockerContainerRequired(testCase.container, oldConfig, changedConfig), true);
    }
}

QTEST_GUILESS_MAIN(InstallControllerTest)
#include "install_controller_test.moc"
