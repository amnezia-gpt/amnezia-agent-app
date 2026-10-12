#include <QtTest/QtTest>

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#include <memory>

#include "core/models/agentWorkloadApply.h"
#include "core/models/protocols/amgptAuthProxyProtocolConfig.h"
#include "core/models/protocols/openClawCodexProtocolConfig.h"
#include "core/utils/selfhosted/sshSession.h"
#include "../public_trust_fixture.h"

namespace
{
    class FakeCommandRunner final : public ISshCommandRunner
    {
    public:
        struct CommandOutcome
        {
            ErrorCode result = ErrorCode::NoError;
            QString stdoutData;
            QString stderrData;
        };

        struct ScpCall
        {
            libssh::ScpOverwriteMode overwriteMode;
            QString localPath;
            QString remotePath;
            QString fileDesc;
            bool localFileExisted = false;
            QByteArray localFileData;
        };

        ErrorCode defaultConnectResult = ErrorCode::NoError;
        ErrorCode defaultCommandResult = ErrorCode::NoError;
        ErrorCode defaultScpResult = ErrorCode::NoError;
        ErrorCode privateKeyResult = ErrorCode::NoError;
        QString privateKeyData;
        QString passphraseResult;
        QList<ErrorCode> connectResults;
        QList<CommandOutcome> commandOutcomes;
        QList<ErrorCode> scpResults;
        QList<QString> commands;
        QList<ScpCall> scpCalls;
        int connectCalls = 0;
        int disconnectCalls = 0;
        int *disconnectCounter = nullptr;
        bool privateKeyCallbackCalled = false;
        ServerCredentials privateKeyCredentials;
        QString observedPassphrase;

        ErrorCode connectToHost(const ServerCredentials &) override
        {
            ++connectCalls;
            if (connectResults.isEmpty())
                return defaultConnectResult;
            return connectResults.takeFirst();
        }

        void disconnectFromHost() override
        {
            ++disconnectCalls;
            if (disconnectCounter)
                ++*disconnectCounter;
        }

        ErrorCode executeCommand(const QString &data, const OutputCallback &cbReadStdOut,
                                 const OutputCallback &cbReadStdErr) override
        {
            commands.append(data);

            CommandOutcome outcome;
            outcome.result = defaultCommandResult;
            if (!commandOutcomes.isEmpty())
                outcome = commandOutcomes.takeFirst();

            if (cbReadStdOut && !outcome.stdoutData.isEmpty()) {
                const ErrorCode callbackError = cbReadStdOut(outcome.stdoutData, callbackClient);
                if (callbackError != ErrorCode::NoError)
                    return callbackError;
            }
            if (cbReadStdErr && !outcome.stderrData.isEmpty()) {
                const ErrorCode callbackError = cbReadStdErr(outcome.stderrData, callbackClient);
                if (callbackError != ErrorCode::NoError)
                    return callbackError;
            }
            return outcome.result;
        }

        ErrorCode scpFileCopy(libssh::ScpOverwriteMode overwriteMode, const QString &localPath,
                              const QString &remotePath, const QString &fileDesc) override
        {
            ScpCall call { overwriteMode, localPath, remotePath, fileDesc };
            QFile localFile(localPath);
            call.localFileExisted = QFileInfo::exists(localPath) && localFile.open(QIODevice::ReadOnly);
            if (call.localFileExisted)
                call.localFileData = localFile.readAll();
            scpCalls.append(call);

            if (scpResults.isEmpty())
                return defaultScpResult;
            return scpResults.takeFirst();
        }

        ErrorCode getDecryptedPrivateKey(const ServerCredentials &credentials, QString &decryptedPrivateKey,
                                         const std::function<QString()> &passphraseCallback) override
        {
            privateKeyCredentials = credentials;
            if (passphraseCallback) {
                privateKeyCallbackCalled = true;
                observedPassphrase = passphraseCallback();
            }
            decryptedPrivateKey = privateKeyData;
            return privateKeyResult;
        }

    private:
        // Output callbacks intentionally retain the production callback type. A
        // fake never uses this client to connect; it only provides the callback's
        // compatibility reference.
        libssh::Client callbackClient;
    };

    ServerCredentials testCredentials()
    {
        ServerCredentials credentials;
        credentials.hostName = QStringLiteral("127.0.0.1");
        credentials.userName = QStringLiteral("test-user");
        credentials.port = 22;
        return credentials;
    }

    QString containerName(DockerContainer container)
    {
        return ContainerUtils::containerToString(container);
    }

    AgentWorkloadDeploymentSpec testOpenClawSpec()
    {
        OpenClawCodexProtocolConfig config;
        const auto spec = makeAgentWorkloadDeploymentSpec(config);
        Q_ASSERT(spec);
        return *spec;
    }

    AgentWorkloadDeploymentSpec
    testAuthProxySpec(const QString &issuer = QStringLiteral("https://auth-dev.example.com"))
    {
        AmgptAuthProxyProtocolConfig config;
        config.backendProfile = QStringLiteral("development");
        config.authIssuer = issuer;
        config.routerBaseUrl = QStringLiteral("https://router-dev.example.com/v1");
        config.runtimeGatewayBaseUrl = QStringLiteral("https://runtime-dev.example.com");
        const auto spec = makeAgentWorkloadDeploymentSpec(withPublicTrust(config));
        Q_ASSERT(spec);
        return *spec;
    }

    QByteArray missingContainerObservation(const AgentWorkloadDeploymentSpec &spec)
    {
        return QJsonDocument(QJsonObject {
                                     { QStringLiteral("schema_version"), 1 },
                                     { QStringLiteral("target"),
                                       QJsonObject { { QStringLiteral("workload"), spec.workload },
                                                     { QStringLiteral("container_name"), spec.containerName },
                                                     { QStringLiteral("host_port"), spec.hostPort } } },
                                     { QStringLiteral("container"), QJsonValue::Null },
                                     { QStringLiteral("host_port_in_use"), false },
                                     { QStringLiteral("port_owners"), QJsonArray() },
                             })
                .toJson(QJsonDocument::Compact);
    }

    QByteArray equivalentContainerObservation(const AgentWorkloadDeploymentSpec &spec, bool running = true)
    {
        const bool published = !spec.hostPort.isEmpty();
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
        const QJsonObject container {
            { QStringLiteral("id"), QStringLiteral("container-id") },
            { QStringLiteral("name"), spec.containerName },
            { QStringLiteral("image_reference"), spec.imageReference },
            { QStringLiteral("image_id"), QStringLiteral("sha256:runtime-image") },
            { QStringLiteral("status"), running ? QStringLiteral("running") : QStringLiteral("exited") },
            { QStringLiteral("running"), running },
            { QStringLiteral("health"), running ? QStringLiteral("healthy") : QStringLiteral("none") },
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
        return QJsonDocument(QJsonObject {
                                     { QStringLiteral("schema_version"), 1 },
                                     { QStringLiteral("target"),
                                       QJsonObject { { QStringLiteral("workload"), spec.workload },
                                                     { QStringLiteral("container_name"), spec.containerName },
                                                     { QStringLiteral("host_port"), spec.hostPort } } },
                                     { QStringLiteral("container"), container },
                                     { QStringLiteral("host_port_in_use"), published && running },
                                     { QStringLiteral("port_owners"),
                                       published ? QJsonArray { QJsonObject {
                                                           { QStringLiteral("id"), QStringLiteral("container-id") },
                                                           { QStringLiteral("name"), spec.containerName } } }
                                                 : QJsonArray {} },
                             })
                .toJson(QJsonDocument::Compact);
    }

    QByteArray imageDriftObservation(const AgentWorkloadDeploymentSpec &spec)
    {
        QJsonDocument document = QJsonDocument::fromJson(equivalentContainerObservation(spec));
        QJsonObject root = document.object();
        QJsonObject container = root.value(QStringLiteral("container")).toObject();
        container.insert(QStringLiteral("image_reference"), QStringLiteral("docker.io/example/old:v0"));
        root.insert(QStringLiteral("container"), container);
        return QJsonDocument(root).toJson(QJsonDocument::Compact);
    }

    QByteArray pendingHealthObservation(const AgentWorkloadDeploymentSpec &spec)
    {
        QJsonDocument document = QJsonDocument::fromJson(equivalentContainerObservation(spec));
        QJsonObject root = document.object();
        QJsonObject container = root.value(QStringLiteral("container")).toObject();
        container.insert(QStringLiteral("health"), QStringLiteral("starting"));
        root.insert(QStringLiteral("container"), container);
        return QJsonDocument(root).toJson(QJsonDocument::Compact);
    }

    QByteArray unmanagedContainerObservation(const AgentWorkloadDeploymentSpec &spec)
    {
        QJsonDocument document = QJsonDocument::fromJson(equivalentContainerObservation(spec));
        QJsonObject root = document.object();
        QJsonObject container = root.value(QStringLiteral("container")).toObject();
        QJsonObject labels = container.value(QStringLiteral("labels")).toObject();
        labels.insert(QStringLiteral("org.amnezia.amgpt.deployment.managed-by"), QStringLiteral("someone-else"));
        container.insert(QStringLiteral("labels"), labels);
        root.insert(QStringLiteral("container"), container);
        return QJsonDocument(root).toJson(QJsonDocument::Compact);
    }

    QString decodedApplyPayload(const QString &command)
    {
        const QString prefix = QStringLiteral("payload='");
        const qsizetype end = command.indexOf(QStringLiteral("';"), prefix.size());
        if (!command.startsWith(prefix) || end < 0) {
            return {};
        }
        return QString::fromUtf8(QByteArray::fromBase64(command.mid(prefix.size(), end - prefix.size()).toLatin1()));
    }
} // namespace

class SshSessionTest final : public QObject
{
    Q_OBJECT

private slots:
    void linksReadChecksExitAndDoesNotCache();
    void linksTransportPreservesUtf8AndRejectsBrokenEncoding();
    void linksExitWrapperSurvivesShell();
    void linksErrorDocumentIsNotNormalAbsence();
    void inlineTrustSurvivesShellAndTransport();
    void enrollmentRecoversAfterFailure();
    void scriptParsingNormalizesCrLfAndSkipsComments();
    void firstCommandFailureStopsFollowingCommands();
    void callbackFailureStopsFollowingCommands();
    void connectionFailureIsPropagated();
    void destructorDisconnectsRunner();
    void uploadFilePassesScpArgumentsAndPayload();
    void containerUploadSequencesOverwriteAndAppend();
    void containerScriptUsesShellAndAlwaysCleansUp();
    void containerExecutionFailureStillRunsCleanup();
    void getTextFileFromContainerDecodesOutput();
    void checkSshConnectionCollectsBothStreams();
    void getDecryptedPrivateKeyDelegatesAllArguments();
    void replaceVarsReplacesAllPairs();
    void agentWorkloadObservationUsesOneBoundedAllowlistedCommand();
    void agentWorkloadObservationRejectsInvalidTargetWithoutSsh();
    void agentWorkloadObservationMapsCommandAndSizeFailures();
    void agentWorkloadApplyRendererUsesExactBoundedPlaybook();
    void agentWorkloadApplyExecutesCompleteDockerRun();
    void gatewayApplyRestartsOnlyOwnedRuntime_data();
    void gatewayApplyRestartsOnlyOwnedRuntime();
    void agentWorkloadApplyRendererRejectsUnsafeInputAndActions();
    void agentWorkloadApplyReturnsNoOpWithoutMutation();
    void agentWorkloadApplyCreatesAndVerifies();
    void agentWorkloadApplyRunsStartAndRecreatePlans();
    void agentWorkloadApplyRejectsStaleOrUnsafePlan();
    void agentWorkloadApplyClassifiesKnownAndUnknownFailures();
    void agentWorkloadLifecycleStopsOnlyOwnedTarget();
    void agentWorkloadLifecycleRemovesOnlyOwnedTarget();
    void agentWorkloadRemovalPurgesStateAndProtectsSharedResources();
    void agentWorkloadRemovalRequiresCleanupConfirmation();
    void agentWorkloadLifecycleRejectsConflictAndClassifiesUnknown();
    void agentWorkloadRemovalRejectsInvalidTargetWithoutSsh();
    void agentLoginStartUsesOwnedHealthyContainerAndExactCommand();
    void agentLoginRejectsUnhealthyContainerBeforeExec();
    void agentLoginBoundsOutputAndParsesStatus();
    void agentLoginReadyRequiresEnrolledRecipient();
    void agentLoginRejectsUnconfirmedRecipient_data();
    void agentLoginRejectsUnconfirmedRecipient();
    void agentLoginDoesNotEnrollBeforeApproval();
};

void SshSessionTest::linksReadChecksExitAndDoesNotCache()
{
    const QString available = R"({"schema_version":1,"links":[{"rel":"urn:amnezia-gpt:link-relation:client-launch:v1","type":"text/html","href":"https://console.example/login?iss=https%3A%2F%2Fauth.example&target_resource=%2F%2Fdevices.amnezia-gpt%2Fdevices%2Fdev_test"}]})";
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    SshSession session(nullptr, std::move(runner));
    for (const auto &suffix : { QString("\nAMNEZIA_LINKS_EXIT=0\n"), QString("\nAMNEZIA_LINKS_EXIT=1\n"),
            QString("\nAMNEZIA_LINKS_EXIT=2\n"), QString("\nAMNEZIA_LINKS_EXIT=124\n"), QString(""),
            QString("\nAMNEZIA_LINKS_EXIT=00\n"), QString("\nAMNEZIA_LINKS_EXIT=0\njunk"),
            QString("\nAMNEZIA_LINKS_EXIT=999\n") }) {
        fake->commandOutcomes = { { ErrorCode::NoError, QString::fromLatin1((available + suffix).toUtf8().toBase64()), "discarded error" } };
        const auto result = session.queryAgentWorkloadLinks(testCredentials());
        QCOMPARE(result.webUi.has_value(), suffix == "\nAMNEZIA_LINKS_EXIT=0\n");
        if (suffix == "\nAMNEZIA_LINKS_EXIT=2\n") QCOMPARE(result.error, AgentWorkloadLinksError::Unsupported);
        QVERIFY(fake->commands.last().contains("timeout 10s sudo -n docker exec -i amnezia-amgpt-device-gateway workloadctl links --json"));
        QVERIFY(!fake->commands.last().contains('\n'));
    }
    fake->commandOutcomes = { { ErrorCode::NoError, QString::fromLatin1(QByteArray(R"({"schema_version":1,"links":[]})" "\nAMNEZIA_LINKS_EXIT=0\n").toBase64()), {} } };
    const auto absent = session.queryAgentWorkloadLinks(testCredentials());
    QCOMPARE(absent.error, AgentWorkloadLinksError::None);
    QVERIFY(!absent.webUi);
    fake->commandOutcomes = { { ErrorCode::ServerDockerFailedError, QString::fromLatin1((available + "\nAMNEZIA_LINKS_EXIT=0\n").toUtf8().toBase64()), {} } };
    QVERIFY(!session.queryAgentWorkloadLinks(testCredentials()).webUi);
    fake->commandOutcomes = { { ErrorCode::NoError, QString(13000, 'a'), {} } };
    QCOMPARE(session.queryAgentWorkloadLinks(testCredentials()).error, AgentWorkloadLinksError::OutputTooLarge);
    QCOMPARE(fake->commands.size(), 11); // One bounded command per user action, never a retry loop.
}

void SshSessionTest::linksErrorDocumentIsNotNormalAbsence()
{
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    SshSession session(nullptr, std::move(runner));
    const QString error = R"({"schema_version":1,"error_code":"gateway-unavailable"})";
    for (const int status : { 1, 0 }) {
        const auto output = (error + QString("\nAMNEZIA_LINKS_EXIT=%1\n").arg(status)).toUtf8().toBase64();
        fake->commandOutcomes = { { ErrorCode::NoError, QString::fromLatin1(output), {} } };
        const auto result = session.queryAgentWorkloadLinks(testCredentials());
        QCOMPARE(result.error, status == 1 ? AgentWorkloadLinksError::CommandFailed : AgentWorkloadLinksError::InvalidResponse);
        QVERIFY(!result.webUi);
    }
    fake->commandOutcomes = { { ErrorCode::NoError, QString::fromLatin1(QByteArray(
        R"({"schema_version":1,"links":[]})" "\nAMNEZIA_LINKS_EXIT=0\n").toBase64()), {} } };
    const auto absent = session.queryAgentWorkloadLinks(testCredentials());
    QCOMPARE(absent.error, AgentWorkloadLinksError::None);
    QVERIFY(!absent.webUi);
}

void SshSessionTest::linksExitWrapperSurvivesShell()
{
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    SshSession session(nullptr, std::move(runner));
    session.queryAgentWorkloadLinks(testCredentials());
    const auto command = fake->commands.last();
    const auto boundary = command.indexOf(';');
    QVERIFY(boundary > 0);
    for (const int status : { 0, 1, 2, 124, 125 }) {
        QProcess shell;
        // Substitute only Docker execution, then execute the actual production status/printf wrapper.
        shell.start("/bin/sh", { "-c", QString("{ (exit %1)").arg(status) + command.mid(boundary) });
        QVERIFY(shell.waitForFinished(5000));
        QCOMPARE(shell.exitCode(), 0);
        QCOMPARE(QByteArray::fromBase64(shell.readAllStandardOutput()), "\nAMNEZIA_LINKS_EXIT=" + QByteArray::number(status) + "\n");
    }
}

void SshSessionTest::linksTransportPreservesUtf8AndRejectsBrokenEncoding()
{
    const QString href = QString::fromUtf8("https://ui.example/устройство#страница");
    const auto document = QJsonDocument(QJsonObject {
        { "schema_version", 1 },
        { "links", QJsonArray { QJsonObject { { "rel", "urn:amnezia-gpt:link-relation:client-launch:v1" }, { "type", "text/html" }, { "href", href } } } }
    }).toJson(QJsonDocument::Compact) + "\nAMNEZIA_LINKS_EXIT=0\n";
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    SshSession session(nullptr, std::move(runner));
    auto encoded = document.toBase64();
    encoded.insert(76, '\n'); // Linux base64 wraps; the transport must tolerate wrapping.
    fake->commandOutcomes = { { ErrorCode::NoError, QString::fromLatin1(encoded) + "\n", {} } };
    const auto result = session.queryAgentWorkloadLinks(testCredentials());
    QVERIFY(result.webUi);
    QCOMPARE(*result.webUi, href);
    for (const auto &broken : { encoded + "!", encoded.chopped(3), document }) {
        fake->commandOutcomes = { { ErrorCode::NoError, QString::fromUtf8(broken), {} } };
        QCOMPARE(session.queryAgentWorkloadLinks(testCredentials()).error, AgentWorkloadLinksError::InvalidResponse);
    }
}

void SshSessionTest::scriptParsingNormalizesCrLfAndSkipsComments()
{
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    fake->commandOutcomes = {
        { ErrorCode::NoError, QStringLiteral("stdout chunk"), QStringLiteral("stderr chunk") },
        { ErrorCode::NoError, QStringLiteral("stdout chunk"), QStringLiteral("stderr chunk") },
    };
    SshSession session(nullptr, std::move(runner));

    QString stdoutText;
    QString stderrText;
    const ErrorCode error = session.runScript(
            testCredentials(),
            QStringLiteral("# ignored\r\n\r\nfirst \\\r\ncontinued\r\n\r\n# ignored too\r\nsecond\r\n"),
            [&stdoutText](const QString &data, libssh::Client &) {
                stdoutText += data;
                return ErrorCode::NoError;
            },
            [&stderrText](const QString &data, libssh::Client &) {
                stderrText += data;
                return ErrorCode::NoError;
            });

    QCOMPARE(error, ErrorCode::NoError);
    QCOMPARE(fake->commands, QList<QString>({ QStringLiteral("first \\\ncontinued"), QStringLiteral("second") }));
    QCOMPARE(stdoutText, QStringLiteral("stdout chunkstdout chunk"));
    QCOMPARE(stderrText, QStringLiteral("stderr chunkstderr chunk"));
    QCOMPARE(fake->connectCalls, 1);
}

void SshSessionTest::firstCommandFailureStopsFollowingCommands()
{
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    fake->commandOutcomes = {
        { ErrorCode::SshInternalError, {}, {} },
        { ErrorCode::NoError, {}, {} },
    };
    SshSession session(nullptr, std::move(runner));

    const ErrorCode error = session.runScript(testCredentials(), QStringLiteral("one\ntwo\nthree\n"));

    QCOMPARE(error, ErrorCode::SshInternalError);
    QCOMPARE(fake->commands, QList<QString>({ QStringLiteral("one") }));
}

void SshSessionTest::callbackFailureStopsFollowingCommands()
{
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    fake->commandOutcomes = {
        { ErrorCode::NoError, QStringLiteral("output"), {} },
        { ErrorCode::NoError, {}, {} },
    };
    SshSession session(nullptr, std::move(runner));

    const ErrorCode error =
            session.runScript(testCredentials(), QStringLiteral("one\ntwo"),
                              [](const QString &, libssh::Client &) { return ErrorCode::ServerCancelInstallation; });

    QCOMPARE(error, ErrorCode::ServerCancelInstallation);
    QCOMPARE(fake->commands, QList<QString>({ QStringLiteral("one") }));
}

void SshSessionTest::connectionFailureIsPropagated()
{
    {
        auto runner = std::make_unique<FakeCommandRunner>();
        auto *fake = runner.get();
        fake->defaultConnectResult = ErrorCode::SshTimeoutError;
        SshSession session(nullptr, std::move(runner));

        QCOMPARE(session.runScript(testCredentials(), QStringLiteral("echo never")), ErrorCode::SshTimeoutError);
        QVERIFY(fake->commands.isEmpty());
    }

    {
        auto runner = std::make_unique<FakeCommandRunner>();
        auto *fake = runner.get();
        fake->defaultConnectResult = ErrorCode::SshRequestDeniedError;
        SshSession session(nullptr, std::move(runner));

        QCOMPARE(session.uploadFileToHost(testCredentials(), QByteArrayLiteral("payload"), QStringLiteral("/tmp/payload")),
                 ErrorCode::SshRequestDeniedError);
        QVERIFY(fake->scpCalls.isEmpty());
    }

    {
        auto runner = std::make_unique<FakeCommandRunner>();
        auto *fake = runner.get();
        fake->defaultScpResult = ErrorCode::SshScpFailureError;
        SshSession session(nullptr, std::move(runner));

        QCOMPARE(session.uploadFileToHost(testCredentials(), QByteArrayLiteral("payload"), QStringLiteral("/tmp/payload")),
                 ErrorCode::SshScpFailureError);
        QCOMPARE(fake->scpCalls.size(), 1);
    }
}

void SshSessionTest::destructorDisconnectsRunner()
{
    int disconnectCounter = 0;
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    fake->disconnectCounter = &disconnectCounter;
    {
        SshSession session(nullptr, std::move(runner));
        QCOMPARE(disconnectCounter, 0);
    }
    QCOMPARE(disconnectCounter, 1);
}

void SshSessionTest::uploadFilePassesScpArgumentsAndPayload()
{
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    SshSession session(nullptr, std::move(runner));

    const QByteArray payload("payload\nwith bytes");
    const ErrorCode error = session.uploadFileToHost(testCredentials(), payload, QStringLiteral("/opt/payload"),
                                                     libssh::ScpOverwriteMode::ScpAppendToExisting);

    QCOMPARE(error, ErrorCode::NoError);
    QCOMPARE(fake->scpCalls.size(), 1);
    const FakeCommandRunner::ScpCall &call = fake->scpCalls.constFirst();
    QCOMPARE(call.overwriteMode, libssh::ScpOverwriteMode::ScpAppendToExisting);
    QCOMPARE(call.remotePath, QStringLiteral("/opt/payload"));
    QCOMPARE(call.fileDesc, QStringLiteral("non_desc"));
    QVERIFY(call.localFileExisted);
    QCOMPARE(call.localFileData, payload);
    QCOMPARE(fake->connectCalls, 1);
}

void SshSessionTest::containerUploadSequencesOverwriteAndAppend()
{
    const auto run = [](libssh::ScpOverwriteMode overwriteMode, int expectedCommandCount) {
        auto runner = std::make_unique<FakeCommandRunner>();
        auto *fake = runner.get();
        SshSession session(nullptr, std::move(runner));

        const QString path = QStringLiteral("/etc/amnezia/config.txt");
        const ErrorCode error = session.uploadTextFileToContainer(DockerContainer::WireGuard, testCredentials(),
                                                                  QStringLiteral("payload"), path, overwriteMode);

        QCOMPARE(error, ErrorCode::NoError);
        QCOMPARE(fake->scpCalls.size(), 1);
        // The temporary host upload always uses the default overwrite mode;
        // the caller's mode only controls the subsequent container operation.
        QCOMPARE(fake->scpCalls.constFirst().overwriteMode, libssh::ScpOverwriteMode::ScpOverwriteExisting);
        QCOMPARE(fake->scpCalls.constFirst().remotePath.left(5), QStringLiteral("/tmp/"));
        QCOMPARE(fake->commands.size(), expectedCommandCount);
        QCOMPARE(fake->connectCalls, expectedCommandCount + 1);
        QVERIFY(fake->commands.at(0).contains(QStringLiteral(" mkdir -p ")));
        QVERIFY(fake->commands.at(1).contains(QStringLiteral(" docker cp ")));
        if (overwriteMode == libssh::ScpOverwriteMode::ScpOverwriteExisting) {
            QVERIFY(fake->commands.at(1).contains(QStringLiteral("://etc/amnezia/config.txt")));
            QVERIFY(fake->commands.at(2).contains(QStringLiteral(" shred -u ")));
        } else {
            QVERIFY(fake->commands.at(1).contains(QStringLiteral("://tmp/")));
            QVERIFY(fake->commands.at(2).contains(QStringLiteral("cat ")));
            QVERIFY(fake->commands.at(3).contains(QStringLiteral(" shred -u ")));
        }
    };

    run(libssh::ScpOverwriteMode::ScpOverwriteExisting, 3);
    run(libssh::ScpOverwriteMode::ScpAppendToExisting, 4);
}

void SshSessionTest::containerScriptUsesShellAndAlwaysCleansUp()
{
    const auto run = [](DockerContainer container, const QString &shell) {
        auto runner = std::make_unique<FakeCommandRunner>();
        auto *fake = runner.get();
        SshSession session(nullptr, std::move(runner));

        const ErrorCode error = session.runContainerScript(testCredentials(), container, QStringLiteral("echo test"));

        QCOMPARE(error, ErrorCode::NoError);
        QCOMPARE(fake->commands.size(), 5);
        QCOMPARE(fake->connectCalls, 6);
        QCOMPARE(fake->scpCalls.size(), 1);
        QCOMPARE(fake->commands.at(0).contains(QStringLiteral(" mkdir -p ")), true);
        QCOMPARE(fake->commands.at(1).contains(QStringLiteral(" docker cp ")), true);
        QCOMPARE(fake->commands.at(2).contains(QStringLiteral(" shred -u ")), true);
        QVERIFY(fake->commands.at(3).contains(QStringLiteral("sudo docker exec -i ")));
        QVERIFY(fake->commands.at(3).contains(QStringLiteral(" %1 ").arg(shell)));
        QVERIFY(fake->commands.at(4).contains(QStringLiteral("sudo docker exec -i ")));
        QVERIFY(fake->commands.at(4).contains(QStringLiteral(" rm ")));
        QVERIFY(fake->commands.at(3).contains(containerName(container)));
    };

    run(DockerContainer::WireGuard, QStringLiteral("bash"));
    run(DockerContainer::Socks5Proxy, QStringLiteral("sh"));
    run(DockerContainer::MtProxy, QStringLiteral("sh"));
    run(DockerContainer::Telemt, QStringLiteral("sh"));
}

void SshSessionTest::containerExecutionFailureStillRunsCleanup()
{
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    fake->commandOutcomes = {
        { ErrorCode::NoError, {}, {} },          { ErrorCode::NoError, {}, {} },         { ErrorCode::NoError, {}, {} },
        { ErrorCode::SshInternalError, {}, {} }, { ErrorCode::SshTimeoutError, {}, {} },
    };
    SshSession session(nullptr, std::move(runner));

    const ErrorCode error =
            session.runContainerScript(testCredentials(), DockerContainer::WireGuard, QStringLiteral("echo test"));

    QCOMPARE(error, ErrorCode::SshInternalError);
    QCOMPARE(fake->commands.size(), 5);
    QVERIFY(fake->commands.constLast().contains(QStringLiteral(" rm ")));
    // Existing behavior returns the script error and ignores a cleanup error.
    QCOMPARE(fake->commandOutcomes.size(), 0);
}

void SshSessionTest::getTextFileFromContainerDecodesOutput()
{
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    fake->commandOutcomes = {
        { ErrorCode::NoError, QStringLiteral("48656c6c6f0a"), {} },
    };
    SshSession session(nullptr, std::move(runner));

    ErrorCode error = ErrorCode::UnknownError;
    const QByteArray data = session.getTextFileFromContainer(DockerContainer::WireGuard, testCredentials(),
                                                             QStringLiteral("/etc/config"), error);

    QCOMPARE(error, ErrorCode::NoError);
    QCOMPARE(data, QByteArrayLiteral("Hello\n"));
    QCOMPARE(fake->commands.size(), 1);
    QCOMPARE(
            fake->commands.constFirst(),
            QStringLiteral(
                    "sudo docker exec -i %1 sh -c \"xxd -p '/etc/config' 2>/dev/null || od -An -v -tx1 '/etc/config'\"")
                    .arg(containerName(DockerContainer::WireGuard)));
}

void SshSessionTest::checkSshConnectionCollectsBothStreams()
{
    {
        auto runner = std::make_unique<FakeCommandRunner>();
        auto *fake = runner.get();
        fake->commandOutcomes = {
            { ErrorCode::NoError, QStringLiteral("uname output"), QStringLiteral("warning") },
        };
        SshSession session(nullptr, std::move(runner));

        ErrorCode error = ErrorCode::UnknownError;
        const QString output = session.checkSshConnection(testCredentials(), error);

        QCOMPARE(error, ErrorCode::NoError);
        QCOMPARE(output, QStringLiteral("uname output\nwarning\n"));
        QCOMPARE(fake->commands, QList<QString>({ QStringLiteral("uname -a") }));
    }

    {
        auto runner = std::make_unique<FakeCommandRunner>();
        auto *fake = runner.get();
        fake->commandOutcomes = {
            { ErrorCode::SshInternalError, {}, QStringLiteral("failure") },
        };
        SshSession session(nullptr, std::move(runner));

        ErrorCode error = ErrorCode::NoError;
        const QString output = session.checkSshConnection(testCredentials(), error);

        QCOMPARE(error, ErrorCode::SshInternalError);
        QCOMPARE(output, QStringLiteral("failure\n"));
    }
}

void SshSessionTest::getDecryptedPrivateKeyDelegatesAllArguments()
{
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    fake->privateKeyResult = ErrorCode::SshPrivateKeyFormatError;
    fake->privateKeyData = QStringLiteral("decrypted-key");
    fake->passphraseResult = QStringLiteral("passphrase");
    SshSession session(nullptr, std::move(runner));

    QString decryptedPrivateKey;
    const ErrorCode error = session.getDecryptedPrivateKey(testCredentials(), decryptedPrivateKey,
                                                           [fake] { return fake->passphraseResult; });

    QCOMPARE(error, ErrorCode::SshPrivateKeyFormatError);
    QCOMPARE(decryptedPrivateKey, QStringLiteral("decrypted-key"));
    QVERIFY(fake->privateKeyCallbackCalled);
    QCOMPARE(fake->observedPassphrase, QStringLiteral("passphrase"));
    QCOMPARE(fake->privateKeyCredentials.hostName, QStringLiteral("127.0.0.1"));
    QCOMPARE(fake->connectCalls, 0);
}

void SshSessionTest::replaceVarsReplacesAllPairs()
{
    const SshSession::Vars vars {
        { QStringLiteral("$ONE"), QStringLiteral("first") },
        { QStringLiteral("$TWO"), QStringLiteral("second") },
    };
    QCOMPARE(SshSession::replaceVars(QStringLiteral("$ONE/$TWO/$ONE"), vars), QStringLiteral("first/second/first"));
}

void SshSessionTest::agentWorkloadObservationUsesOneBoundedAllowlistedCommand()
{
    const auto spec = testOpenClawSpec();
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    fake->commandOutcomes = {
        { ErrorCode::NoError, QString::fromUtf8(missingContainerObservation(spec)), QStringLiteral("discarded") },
    };
    SshSession session(nullptr, std::move(runner));

    const auto result = session.observeAgentWorkload(testCredentials(), spec);

    QCOMPARE(result.error, AgentWorkloadObservationError::None);
    QVERIFY(result.state);
    QVERIFY(!result.state->containerPresent);
    QCOMPARE(fake->commands.size(), 1);
    const QString command = fake->commands.constFirst();
    QVERIFY(!command.contains(QLatin1Char('\n')));
    QVERIFY(command.contains(QStringLiteral("docker container inspect --format")));
    QVERIFY(!command.contains(QStringLiteral("--type")));
    // An unpublished workload has no host port to probe or claim.
    QVERIFY(!command.contains(QStringLiteral("lsof")));
    QVERIFY(!command.contains(QStringLiteral("publish=")));
    QVERIFY(command.contains(spec.containerName));
    QVERIFY(command.contains(QStringLiteral("org.amnezia.amgpt.deployment.spec-hash")));
    QVERIFY(!command.contains(QStringLiteral("%%s")));
    QVERIFY(command.contains(QStringLiteral("range .Config.Env")));
    QVERIFY(!command.contains(QStringLiteral("json .Config.Env")));
    QVERIFY(command.contains(QStringLiteral("AMGPT_INGRESS_TRUST_BUNDLE")));
    QVERIFY(command.contains(QStringLiteral("AMGPT_INGRESS_TRUST_SHA256")));
    QVERIFY(command.contains(QStringLiteral("\"read_only\":{{json (not .RW)}}")));
    QVERIFY(!command.contains(QStringLiteral("codex-app-server.token")));
    QVERIFY(!command.contains(QStringLiteral("AMGPT_CODEX_APP_TOKEN")));
    QVERIFY(!command.contains(QStringLiteral("ACCESS_TOKEN")));
    QVERIFY(!command.contains(QStringLiteral("session.json")));

    QProcess shell;
    shell.start(QStringLiteral("/bin/sh"), { QStringLiteral("-n"), QStringLiteral("-c"), command });
    QVERIFY(shell.waitForFinished());
    QCOMPARE(shell.exitCode(), 0);

    auto published = spec;
    published.hostPort = QStringLiteral("18443");
    auto publishedRunner = std::make_unique<FakeCommandRunner>();
    auto *publishedFake = publishedRunner.get();
    publishedFake->commandOutcomes = {
        { ErrorCode::NoError, QString::fromUtf8(missingContainerObservation(published)), {} },
    };
    SshSession publishedSession(nullptr, std::move(publishedRunner));
    QCOMPARE(publishedSession.observeAgentWorkload(testCredentials(), published).error,
             AgentWorkloadObservationError::None);
    const QString publishedCommand = publishedFake->commands.constFirst();
    QVERIFY(publishedCommand.contains(QStringLiteral("lsof -nP -iTCP:")));
    QVERIFY(publishedCommand.contains(QStringLiteral("publish=${port}/tcp")));
    QVERIFY(publishedCommand.contains(QStringLiteral("port='18443'")));
    shell.start(QStringLiteral("/bin/sh"), { QStringLiteral("-n"), QStringLiteral("-c"), publishedCommand });
    QVERIFY(shell.waitForFinished());
    QCOMPARE(shell.exitCode(), 0);
}

void SshSessionTest::agentWorkloadObservationRejectsInvalidTargetWithoutSsh()
{
    auto spec = testOpenClawSpec();
    spec.containerName = QStringLiteral("$(untrusted)");
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    SshSession session(nullptr, std::move(runner));

    const auto result = session.observeAgentWorkload(testCredentials(), spec);

    QCOMPARE(result.error, AgentWorkloadObservationError::TargetMismatch);
    QVERIFY(!result.state);
    QVERIFY(fake->commands.isEmpty());
    QCOMPARE(fake->connectCalls, 0);
}

void SshSessionTest::agentWorkloadObservationMapsCommandAndSizeFailures()
{
    const auto spec = testOpenClawSpec();
    {
        auto runner = std::make_unique<FakeCommandRunner>();
        runner->commandOutcomes = { { ErrorCode::SshTimeoutError, {}, QStringLiteral("sensitive stderr") } };
        SshSession session(nullptr, std::move(runner));
        QCOMPARE(session.observeAgentWorkload(testCredentials(), spec).error,
                 AgentWorkloadObservationError::CommandFailed);
    }
    {
        auto runner = std::make_unique<FakeCommandRunner>();
        runner->commandOutcomes = {
            { ErrorCode::NoError, QString(AgentWorkloadObservationMaxBytes + 1, QLatin1Char('x')), {} },
        };
        SshSession session(nullptr, std::move(runner));
        QCOMPARE(session.observeAgentWorkload(testCredentials(), spec).error,
                 AgentWorkloadObservationError::OutputTooLarge);
    }
}

void SshSessionTest::agentWorkloadApplyRendererUsesExactBoundedPlaybook()
{
    const auto spec = testOpenClawSpec();
    AgentWorkloadApplyRenderError error = AgentWorkloadApplyRenderError::None;
    const auto script = renderAgentWorkloadApplyScript(spec, AgentWorkloadReconciliationAction::Create, &error);

    QVERIFY(script);
    QCOMPARE(error, AgentWorkloadApplyRenderError::None);
    QVERIFY(script->startsWith(QStringLiteral("set -eu\n")));
    QVERIFY(script->contains(QStringLiteral("action='create'")));
    QVERIFY(script->contains(QStringLiteral("platform='linux/amd64'")));
    QVERIFY(script->contains(QStringLiteral("docker image pull --platform \"$platform\" \"$image\"")));
    QVERIFY(script->contains(QStringLiteral("docker container run --detach --pull=never")));
    QVERIFY(script->contains(QStringLiteral("--restart 'unless-stopped'")));
    QVERIFY(script->contains(QStringLiteral("--cap-drop 'ALL'")));
    QVERIFY(!script->contains(QStringLiteral("--cap-add")));
    QVERIFY(script->contains(QStringLiteral("--network-alias 'codex-app-server'")));
    QVERIFY(script->contains(QStringLiteral("--volume 'amnezia-agent-codex-app-socket:/run/amgpt-codex:ro'")));
    QVERIFY(!script->contains(QStringLiteral("--entrypoint")));
    QVERIFY(!script->contains(QStringLiteral("--ws-token")));
    QVERIFY(!script->contains(QStringLiteral("--publish")));
    QVERIFY(!script->contains(QStringLiteral("--user")));
    QVERIFY(script->contains(QStringLiteral("--security-opt 'no-new-privileges'")));
    QVERIFY(!script->contains(QStringLiteral("--health-cmd")));
    QVERIFY(!script->contains(QStringLiteral("--network-alias ''")));
    QVERIFY(script->contains(QStringLiteral("AMNEZIA_AGENT_APPLY_APPLIED")));
    QVERIFY(!script->contains(QStringLiteral("docker volume rm")));
    QVERIFY(!script->contains(QStringLiteral("docker network rm")));
    QVERIFY(!script->contains(QStringLiteral("amnezia-amgpt-device-gateway-state")));

    QProcess shell;
    shell.start(QStringLiteral("/bin/sh"), { QStringLiteral("-n"), QStringLiteral("-c"), *script });
    QVERIFY(shell.waitForFinished());
    QCOMPARE(shell.exitCode(), 0);

    const auto proxy = testAuthProxySpec(QStringLiteral("https://auth-dev.example.com/tenant;printf"));
    const auto proxyScript = renderAgentWorkloadApplyScript(proxy, AgentWorkloadReconciliationAction::Recreate);
    QVERIFY(proxyScript);
    QVERIFY(!proxyScript->contains(QStringLiteral("--health-cmd")));
    QVERIFY(proxyScript->contains(QStringLiteral("--network-alias 'amgpt-device-gateway'")));
    QVERIFY(!proxyScript->contains(QStringLiteral("--publish")));
    QVERIFY(!proxyScript->contains(QStringLiteral("--cap-add")));
    QVERIFY(!proxyScript->contains(QStringLiteral("--user")));
    QVERIFY(proxyScript->contains(QStringLiteral("--volume 'amnezia-agent-codex-app-socket:/run/amgpt-codex'")));
    QVERIFY(proxyScript->contains(
            QStringLiteral("--env 'AMGPT_AUTH_ISSUER=https://auth-dev.example.com/tenant;printf'")));
    QVERIFY(proxyScript->contains(QStringLiteral("--env 'AMGPT_ROUTER_BASE_URL=https://router-dev.example.com/v1'")));
    QVERIFY(proxyScript->contains(
            QStringLiteral("--env 'AMGPT_RUNTIME_GATEWAY_BASE_URL=https://runtime-dev.example.com'")));
    QVERIFY(proxyScript->contains(
            QStringLiteral("--label 'org.amnezia.amgpt.deployment.backend-profile=development'")));
    QVERIFY(!proxyScript->contains(QStringLiteral("amnezia-openclaw-state")));
}

void SshSessionTest::inlineTrustSurvivesShellAndTransport()
{
    auto config = AmgptAuthProxyProtocolConfig {};
    config.backendProfile = "local";
    config.authIssuer = "https://auth.example";
    config.routerBaseUrl = "https://router.example/v1";
    config.runtimeGatewayBaseUrl = "https://runtime.example";
    config = withPublicTrust(config, "key'$(printf 'INJECTED\\n' >&3);@@IMAGE@@@@UNKNOWN@@");
    config.ingressTrustBundle.replace("\n", "\r\n");
    config.ingressTrustSha256 = trustPin(config.ingressTrustBundle);
    const auto spec = makeAgentWorkloadDeploymentSpec(config);
    QVERIFY(spec);
    const auto script = renderAgentWorkloadApplyScript(*spec, AgentWorkloadReconciliationAction::Create);
    QVERIFY(script);
    const auto fake = QString::fromLatin1(R"(
exec 3>&1
docker() {
    case "$1 $2" in
        'network inspect'|'volume inspect'|'image pull'|'container ls') return 0 ;;
        'container inspect') printf 'healthy\n' ;;
        'container run') printf '%s\000' "$@" >&3 ;;
        *) return 91 ;;
    esac
}
)");
    QProcess shell;
    shell.start("/bin/sh", { "-c", "sudo() { shift; \"$@\"; }; " + agentWorkloadApplyCommand(fake + *script) });
    QVERIFY(shell.waitForFinished(5000));
    QCOMPARE(shell.exitCode(), 0);
    const auto output = shell.readAllStandardOutput();
    const auto args = output.split('\0');
    QVERIFY(args.contains("AMGPT_INGRESS_TRUST_BUNDLE=" + config.ingressTrustBundle.toUtf8()));
    QVERIFY(args.contains("AMGPT_INGRESS_TRUST_SHA256=" + config.ingressTrustSha256.toUtf8()));
    QVERIFY(!output.contains("INJECTED\n"));
    QVERIFY(!args.contains("--publish"));
}

void SshSessionTest::agentWorkloadApplyExecutesCompleteDockerRun()
{
    for (const auto &spec : { testOpenClawSpec(), testAuthProxySpec() }) {
        const auto script = renderAgentWorkloadApplyScript(spec, AgentWorkloadReconciliationAction::Create);
        QVERIFY(script);
        // Every Docker call is intercepted; fd 3 captures argv despite production redirections.
        const QString fake = QString::fromLatin1(R"(
exec 3>&1
docker() {
    case "$1 $2" in
        'network inspect'|'volume inspect'|'image pull'|'container ls') return 0 ;;
        'container inspect') printf 'healthy\n'; return 0 ;;
        'container run')
            printf 'RUN\n' >&3
            printf '<%s>\n' "$@" >&3
            for argument in "$@"; do last_argument=$argument; done
            [ "$last_argument" = "$image" ] ;;
        *) return 91 ;;
    esac
}
)");
        QProcess shell;
        shell.start(QStringLiteral("/bin/sh"), { QStringLiteral("-c"), fake + *script });
        QVERIFY(shell.waitForFinished(5000));
        const auto output = shell.readAllStandardOutput();
        QCOMPARE(shell.exitCode(), 0);
        QCOMPARE(output.count("RUN\n"), 1);
        QVERIFY(output.contains((QLatin1Char('<') + spec.imageReference + QStringLiteral(">\n")).toUtf8()));
        QVERIFY(!output.contains("<--publish>"));
        QCOMPARE(output.contains("<--cap-add>"), !spec.capabilitiesAdded.isEmpty());
        QCOMPARE(output.contains("<--network-alias>"), !spec.networkAlias.isEmpty());
        QVERIFY(output.contains("<--health-start-period>"));
        QCOMPARE(output.contains("<AMGPT_RUNTIME_GATEWAY_BASE_URL=https://runtime-dev.example.com>"),
                 spec.workload == QStringLiteral("amgpt-device-gateway"));
        QVERIFY(output.contains("AMNEZIA_AGENT_APPLY_APPLIED"));
    }
}

void SshSessionTest::gatewayApplyRestartsOnlyOwnedRuntime_data()
{
    QTest::addColumn<AgentWorkloadReconciliationAction>("action");
    QTest::addColumn<QString>("runtime");
    QTest::addColumn<bool>("restart");
    QTest::addColumn<bool>("success");
    using Action = AgentWorkloadReconciliationAction;
    QTest::newRow("create-after-runtime") << Action::Create << QString("owned") << true << true;
    QTest::newRow("recreate-after-runtime") << Action::Recreate << QString("owned") << true << true;
    QTest::newRow("gateway-first") << Action::Create << QString("absent") << false << true;
    QTest::newRow("foreign-manager") << Action::Create << QString("foreign-manager") << false << true;
    QTest::newRow("foreign-workload") << Action::Recreate << QString("foreign-workload") << false << true;
    QTest::newRow("start-keeps-token") << Action::Start << QString("owned") << false << true;
    QTest::newRow("restart-fails") << Action::Create << QString("restart-fails") << true << false;
    QTest::newRow("runtime-stopped") << Action::Recreate << QString("stopped") << true << false;
    QTest::newRow("runtime-no-healthcheck") << Action::Create << QString("no-health") << true << false;
    QTest::newRow("runtime-health-timeout") << Action::Create << QString("starting") << true << false;
    QTest::newRow("owner-inspect-fails") << Action::Create << QString("inspect-fails") << false << false;
    QTest::newRow("ambiguous-runtime") << Action::Create << QString("ambiguous") << false << false;
    QTest::newRow("gateway-stopped") << Action::Create << QString("gateway-stopped") << false << false;
}

void SshSessionTest::gatewayApplyRestartsOnlyOwnedRuntime()
{
    QFETCH(AgentWorkloadReconciliationAction, action);
    QFETCH(QString, runtime);
    QFETCH(bool, restart);
    QFETCH(bool, success);
    const auto script = renderAgentWorkloadApplyScript(testAuthProxySpec(), action);
    QVERIFY(script);
    // A shell fake models the Docker boundary; no daemon or remote host is contacted.
    const auto fake = QString::fromLatin1(R"(
exec 3>&1
sleep() { printf 'WAIT\n' >&3; }
docker() {
    case "$1 $2" in
        'network inspect'|'volume inspect'|'image pull'|'container run'|'container rm'|'container start') return 0 ;;
        'container ls')
            case "$*" in
                *amnezia-openclaw-codex*)
                    case "$runtime" in
                        absent) return 0 ;;
                        ambiguous) printf 'runtime-id\nother-id\n' ;;
                        *) printf 'runtime-id\n' ;;
                    esac ;;
                *) [ "$action" = create ] || printf 'gateway-id\n' ;;
            esac ;;
        'container inspect')
            case "$4" in
                *managed-by*)
                    case "$5:$runtime" in
                        runtime-id:inspect-fails) return 91 ;;
                        runtime-id:foreign-manager) printf 'other-manager\n' ;;
                        *) printf 'amnezia-agent-app\n' ;;
                    esac ;;
                *deployment.workload*)
                    case "$5:$runtime" in
                        runtime-id:foreign-workload) printf 'other-workload\n' ;;
                        runtime-id:*) printf 'openclaw-codex\n' ;;
                        *) printf 'amgpt-device-gateway\n' ;;
                    esac ;;
                *)
                    case "$5:$runtime" in
                        runtime-id:stopped) printf 'stopped\n' ;;
                        runtime-id:no-health) printf 'none\n' ;;
                        runtime-id:starting) printf 'starting\n' ;;
                        runtime-id:*) printf 'RUNTIME-HEALTH\n' >&3; printf 'healthy\n' ;;
                        *)
                            printf 'GATEWAY-HEALTH\n' >&3
                            if [ "$runtime" = gateway-stopped ]; then printf 'stopped\n'; else printf 'healthy\n'; fi ;;
                    esac ;;
            esac ;;
        'container restart')
            printf 'RESTART:%s\n' "$5" >&3
            [ "$runtime" != restart-fails ] ;;
        *) return 92 ;;
    esac
}
)");
    QProcess shell;
    shell.start("/bin/sh", { "-c", QString("runtime='%1';\n").arg(runtime) + fake + *script });
    QVERIFY(shell.waitForFinished(5000));
    const auto output = shell.readAllStandardOutput();
    QCOMPARE(shell.exitCode() == 0, success);
    QCOMPARE(output.count("RESTART:runtime-id\n"), restart ? 1 : 0);
    QCOMPARE(output.contains("AMNEZIA_AGENT_APPLY_APPLIED"), success);
    if (restart) {
        QVERIFY(output.indexOf("GATEWAY-HEALTH") < output.indexOf("RESTART:"));
        if (success) QVERIFY(output.indexOf("RESTART:") < output.indexOf("RUNTIME-HEALTH"));
    }
    QVERIFY(output.count("WAIT\n") <= 90);
}

void SshSessionTest::agentWorkloadApplyRendererRejectsUnsafeInputAndActions()
{
    auto changed = testOpenClawSpec();
    changed.imageReference = QStringLiteral("$(touch /tmp/untrusted)");
    AgentWorkloadApplyRenderError error = AgentWorkloadApplyRenderError::None;
    QVERIFY(!renderAgentWorkloadApplyScript(changed, AgentWorkloadReconciliationAction::Create, &error));
    QCOMPARE(error, AgentWorkloadApplyRenderError::InvalidDesiredState);

    const auto valid = testOpenClawSpec();
    QVERIFY(!renderAgentWorkloadApplyScript(valid, AgentWorkloadReconciliationAction::Conflict, &error));
    QCOMPARE(error, AgentWorkloadApplyRenderError::UnsupportedAction);
    QVERIFY(!renderAgentWorkloadApplyScript(valid, AgentWorkloadReconciliationAction::NoOp, &error));
    QCOMPARE(error, AgentWorkloadApplyRenderError::UnsupportedAction);
}

void SshSessionTest::agentWorkloadApplyReturnsNoOpWithoutMutation()
{
    for (const auto &spec : { testOpenClawSpec(), testAuthProxySpec() }) {
        auto runner = std::make_unique<FakeCommandRunner>();
        auto *fake = runner.get();
        fake->commandOutcomes = {
            { ErrorCode::NoError, QString::fromUtf8(equivalentContainerObservation(spec)), {} },
        };
        SshSession session(nullptr, std::move(runner));

        const auto result = session.applyAgentWorkloadPlan(
                testCredentials(), spec,
                { AgentWorkloadReconciliationAction::NoOp, AgentWorkloadReconciliationReason::EquivalentAndRunning });

        QCOMPARE(result.status, AgentWorkloadApplyStatus::NoOp);
        QCOMPARE(result.reason, AgentWorkloadApplyReason::None);
        QCOMPARE(fake->commands.size(), 1);
    }
}

void SshSessionTest::agentWorkloadApplyCreatesAndVerifies()
{
    for (const auto &spec : { testOpenClawSpec(), testAuthProxySpec() }) {
        auto runner = std::make_unique<FakeCommandRunner>();
        auto *fake = runner.get();
        fake->commandOutcomes = {
            { ErrorCode::NoError, QString::fromUtf8(missingContainerObservation(spec)), {} },
            { ErrorCode::NoError, QStringLiteral("AMNEZIA_AGENT_APPLY_APPLIED\n"), QStringLiteral("discarded") },
            { ErrorCode::NoError, QString::fromUtf8(equivalentContainerObservation(spec)), {} },
        };
        SshSession session(nullptr, std::move(runner));

        const auto result = session.applyAgentWorkloadPlan(
                testCredentials(), spec,
                { AgentWorkloadReconciliationAction::Create, AgentWorkloadReconciliationReason::ContainerMissing });

        QCOMPARE(result.status, AgentWorkloadApplyStatus::Applied);
        QCOMPARE(result.reason, AgentWorkloadApplyReason::None);
        QCOMPARE(fake->commands.size(), 3);
        const QString applyCommand = fake->commands.at(1);
        QVERIFY(!applyCommand.contains(QLatin1Char('\n')));
        QVERIFY(applyCommand.startsWith(QStringLiteral("payload='")));
        QVERIFY(applyCommand.contains(QStringLiteral("printf '%s' \"$payload\"")));
        QVERIFY(applyCommand.endsWith(QStringLiteral("base64 -d | sudo -n sh")));
        QVERIFY(!applyCommand.contains(QStringLiteral("AMGPT_AUTH_ISSUER")));
        const QString payload = decodedApplyPayload(applyCommand);
        QVERIFY(payload.contains(spec.containerName));
        QVERIFY(payload.contains(spec.imageReference));
    }
}

void SshSessionTest::agentWorkloadApplyRunsStartAndRecreatePlans()
{
    for (const auto &spec : { testOpenClawSpec(), testAuthProxySpec() }) {
        const auto run = [&](AgentWorkloadReconciliationAction action, AgentWorkloadReconciliationReason reason,
                             const QByteArray &preflight, const QString &renderedAction) {
            auto runner = std::make_unique<FakeCommandRunner>();
            auto *fake = runner.get();
            fake->commandOutcomes = {
                { ErrorCode::NoError, QString::fromUtf8(preflight), {} },
                { ErrorCode::NoError, QStringLiteral("AMNEZIA_AGENT_APPLY_APPLIED\n"), {} },
                { ErrorCode::NoError, QString::fromUtf8(equivalentContainerObservation(spec)), {} },
            };
            SshSession session(nullptr, std::move(runner));
            const auto result = session.applyAgentWorkloadPlan(testCredentials(), spec, { action, reason });
            QCOMPARE(result.status, AgentWorkloadApplyStatus::Applied);
            QCOMPARE(fake->commands.size(), 3);
            QVERIFY(decodedApplyPayload(fake->commands.at(1)).contains(QStringLiteral("action='%1'").arg(renderedAction)));
        };

        run(AgentWorkloadReconciliationAction::Start, AgentWorkloadReconciliationReason::EquivalentButStopped,
            equivalentContainerObservation(spec, false), QStringLiteral("start"));
        run(AgentWorkloadReconciliationAction::Recreate, AgentWorkloadReconciliationReason::ImageDrift,
            imageDriftObservation(spec), QStringLiteral("recreate"));
    }
}

void SshSessionTest::agentWorkloadApplyRejectsStaleOrUnsafePlan()
{
    const auto spec = testOpenClawSpec();
    {
        auto runner = std::make_unique<FakeCommandRunner>();
        auto *fake = runner.get();
        SshSession session(nullptr, std::move(runner));
        const auto result = session.applyAgentWorkloadPlan(testCredentials(), spec,
                                                           { AgentWorkloadReconciliationAction::Conflict,
                                                             AgentWorkloadReconciliationReason::ContainerOwnedByOther });
        QCOMPARE(result.status, AgentWorkloadApplyStatus::Failed);
        QCOMPARE(result.reason, AgentWorkloadApplyReason::UnsupportedPlan);
        QVERIFY(fake->commands.isEmpty());
    }
    {
        auto runner = std::make_unique<FakeCommandRunner>();
        auto *fake = runner.get();
        fake->commandOutcomes = {
            { ErrorCode::NoError, QString::fromUtf8(equivalentContainerObservation(spec, false)), {} },
        };
        SshSession session(nullptr, std::move(runner));
        const auto result = session.applyAgentWorkloadPlan(
                testCredentials(), spec,
                { AgentWorkloadReconciliationAction::Create, AgentWorkloadReconciliationReason::ContainerMissing });
        QCOMPARE(result.status, AgentWorkloadApplyStatus::Failed);
        QCOMPARE(result.reason, AgentWorkloadApplyReason::StalePlan);
        QCOMPARE(fake->commands.size(), 1);
    }
    {
        auto changed = spec;
        changed.containerName = QStringLiteral("untrusted");
        auto runner = std::make_unique<FakeCommandRunner>();
        auto *fake = runner.get();
        SshSession session(nullptr, std::move(runner));
        const auto result = session.applyAgentWorkloadPlan(
                testCredentials(), changed,
                { AgentWorkloadReconciliationAction::Create, AgentWorkloadReconciliationReason::ContainerMissing });
        QCOMPARE(result.status, AgentWorkloadApplyStatus::Failed);
        QCOMPARE(result.reason, AgentWorkloadApplyReason::InvalidDesiredState);
        QVERIFY(fake->commands.isEmpty());
    }
}

void SshSessionTest::agentWorkloadApplyClassifiesKnownAndUnknownFailures()
{
    const auto spec = testOpenClawSpec();
    const auto plan = AgentWorkloadReconciliationPlan { AgentWorkloadReconciliationAction::Create,
                                                        AgentWorkloadReconciliationReason::ContainerMissing };
    const auto run = [&](const FakeCommandRunner::CommandOutcome &mutation) {
        auto runner = std::make_unique<FakeCommandRunner>();
        runner->commandOutcomes = {
            { ErrorCode::NoError, QString::fromUtf8(missingContainerObservation(spec)), {} },
            mutation,
        };
        SshSession session(nullptr, std::move(runner));
        return session.applyAgentWorkloadPlan(testCredentials(), spec, plan);
    };

    auto result = run({ ErrorCode::NoError,
                        QStringLiteral("AMNEZIA_AGENT_APPLY_HEALTH_TIMEOUT\nAMNEZIA_AGENT_APPLY_FAILED:75\n"),
                        {} });
    QCOMPARE(result.status, AgentWorkloadApplyStatus::Failed);
    QCOMPARE(result.reason, AgentWorkloadApplyReason::HealthTimeout);

    result = run({ ErrorCode::SshTimeoutError, {}, {} });
    QCOMPARE(result.status, AgentWorkloadApplyStatus::Unknown);
    QCOMPARE(result.reason, AgentWorkloadApplyReason::CommandFailed);

    result = run({ ErrorCode::ServerCancelInstallation, {}, {} });
    QCOMPARE(result.status, AgentWorkloadApplyStatus::Unknown);
    QCOMPARE(result.reason, AgentWorkloadApplyReason::CommandFailed);

    result = run({ ErrorCode::NoError, QStringLiteral("no terminal marker"), {} });
    QCOMPARE(result.status, AgentWorkloadApplyStatus::Unknown);
    QCOMPARE(result.reason, AgentWorkloadApplyReason::MissingTerminalMarker);

    result = run({ ErrorCode::NoError, QStringLiteral("AMNEZIA_AGENT_APPLY_FAILED:70\n"), {} });
    QCOMPARE(result.status, AgentWorkloadApplyStatus::Failed);
    QCOMPARE(result.reason, AgentWorkloadApplyReason::ScriptFailed);

    result = run({ ErrorCode::NoError, QString(AgentWorkloadApplyOutputMaxBytes + 1, QLatin1Char('x')), {} });
    QCOMPARE(result.status, AgentWorkloadApplyStatus::Unknown);
    QCOMPARE(result.reason, AgentWorkloadApplyReason::OutputTooLarge);

    {
        auto runner = std::make_unique<FakeCommandRunner>();
        runner->commandOutcomes = { { ErrorCode::SshTimeoutError, {}, {} } };
        SshSession session(nullptr, std::move(runner));
        result = session.applyAgentWorkloadPlan(testCredentials(), spec, plan);
        QCOMPARE(result.status, AgentWorkloadApplyStatus::Failed);
        QCOMPARE(result.reason, AgentWorkloadApplyReason::PreflightObservationFailed);
    }
    {
        auto runner = std::make_unique<FakeCommandRunner>();
        runner->commandOutcomes = {
            { ErrorCode::NoError, QString::fromUtf8(missingContainerObservation(spec)), {} },
            { ErrorCode::NoError, QStringLiteral("AMNEZIA_AGENT_APPLY_APPLIED\n"), {} },
            { ErrorCode::SshTimeoutError, {}, {} },
        };
        SshSession session(nullptr, std::move(runner));
        result = session.applyAgentWorkloadPlan(testCredentials(), spec, plan);
        QCOMPARE(result.status, AgentWorkloadApplyStatus::Unknown);
        QCOMPARE(result.reason, AgentWorkloadApplyReason::PostApplyObservationFailed);
    }
    {
        auto runner = std::make_unique<FakeCommandRunner>();
        runner->commandOutcomes = {
            { ErrorCode::NoError, QString::fromUtf8(missingContainerObservation(spec)), {} },
            { ErrorCode::NoError, QStringLiteral("AMNEZIA_AGENT_APPLY_APPLIED\n"), {} },
            { ErrorCode::NoError, QString::fromUtf8(missingContainerObservation(spec)), {} },
        };
        SshSession session(nullptr, std::move(runner));
        result = session.applyAgentWorkloadPlan(testCredentials(), spec, plan);
        QCOMPARE(result.status, AgentWorkloadApplyStatus::Failed);
        QCOMPARE(result.reason, AgentWorkloadApplyReason::PostApplyDrift);
    }
    {
        auto runner = std::make_unique<FakeCommandRunner>();
        runner->commandOutcomes = {
            { ErrorCode::NoError, QString::fromUtf8(missingContainerObservation(spec)), {} },
            { ErrorCode::NoError, QStringLiteral("AMNEZIA_AGENT_APPLY_APPLIED\n"), {} },
            { ErrorCode::NoError, QString::fromUtf8(pendingHealthObservation(spec)), {} },
        };
        SshSession session(nullptr, std::move(runner));
        result = session.applyAgentWorkloadPlan(testCredentials(), spec, plan);
        QCOMPARE(result.status, AgentWorkloadApplyStatus::Unknown);
        QCOMPARE(result.reason, AgentWorkloadApplyReason::PostApplyObservationFailed);
    }
}

void SshSessionTest::agentWorkloadLifecycleStopsOnlyOwnedTarget()
{
    const auto spec = testOpenClawSpec();
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    fake->commandOutcomes = {
        { ErrorCode::NoError, QString::fromUtf8(equivalentContainerObservation(spec)), {} },
        { ErrorCode::NoError, {}, {} },
        { ErrorCode::NoError, QString::fromUtf8(equivalentContainerObservation(spec, false)), {} },
    };
    SshSession session(nullptr, std::move(runner));

    const auto result = session.changeAgentWorkloadLifecycle(testCredentials(), spec, AgentWorkloadLifecycleAction::Stop);

    QCOMPARE(result.status, AgentWorkloadLifecycleStatus::Applied);
    QCOMPARE(result.reason, AgentWorkloadLifecycleReason::None);
    QCOMPARE(fake->commands.size(), 3);
    QVERIFY(fake->commands.at(1).contains(QStringLiteral("docker container stop")));
    QVERIFY(fake->commands.at(1).contains(spec.containerName));
    QVERIFY(!fake->commands.at(1).contains(QStringLiteral("docker volume")));
    QVERIFY(!fake->commands.at(1).contains(QStringLiteral("docker network")));
    QVERIFY(!fake->commands.at(1).contains(QStringLiteral("amnezia-amgpt-device-gateway")));
}

void SshSessionTest::agentWorkloadLifecycleRemovesOnlyOwnedTarget()
{
    const auto spec = testAuthProxySpec();
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    fake->commandOutcomes = {
        { ErrorCode::NoError, QString::fromUtf8(equivalentContainerObservation(spec)), {} },
        { ErrorCode::NoError, QStringLiteral("AMNEZIA_AGENT_REMOVE_COMPLETE\n"), {} },
        { ErrorCode::NoError, QString::fromUtf8(missingContainerObservation(spec)), {} },
    };
    SshSession session(nullptr, std::move(runner));

    const auto result =
            session.changeAgentWorkloadLifecycle(testCredentials(), spec, AgentWorkloadLifecycleAction::Remove);

    QCOMPARE(result.status, AgentWorkloadLifecycleStatus::Applied);
    QCOMPARE(fake->commands.size(), 3);
    const auto cleanup = decodedApplyPayload(fake->commands.at(1));
    QVERIFY(cleanup.contains(QStringLiteral("docker container rm --force")));
    QVERIFY(cleanup.contains(QStringLiteral("docker volume rm")));
    QVERIFY(cleanup.contains(QStringLiteral("amnezia-amgpt-device-gateway-state")));
}

void SshSessionTest::agentWorkloadRemovalPurgesStateAndProtectsSharedResources()
{
#ifdef Q_OS_WINDOWS
    QSKIP("POSIX shell execution is covered on Unix hosts");
#else
    struct Scenario {
        const char *name;
        bool present;
        bool sharedInUse;
        bool deletionFails;
        bool volumeRemains;
        bool foreignOwner;
        bool dockerFails;
    };
    const QList<Scenario> scenarios {
        { "last-workload", true, false, false, false, false, false },
        { "stopped-sibling-keeps-shared-resources", true, true, false, false, false, false },
        { "retry-after-container-removed", false, false, false, false, false, false },
        { "private-volume-in-use", true, false, true, false, false, false },
        { "verify-private-volume-absence", true, false, false, true, false, false },
        { "ownership-changed-after-observation", true, false, false, false, true, false },
        { "docker-list-error-is-not-absence", true, false, false, false, false, true },
    };
    const QString fakeDocker = QString::fromLatin1(R"(
exec 3>&1
docker() {
    case "$1 $2" in
        'container ls')
            for arg in "$@"; do filter=$arg; done
            case "$filter" in
                name=*) [ "$present" = 0 ] || printf 'target-id\n' ;;
                volume=*|network=*|ancestor=*) [ "$shared_in_use" = 0 ] || printf 'stopped-sibling-id\n' ;;
                *) return 91 ;;
            esac
            return 0 ;;
        'container inspect')
            case "$*" in
                *managed-by*) if [ "$foreign_owner" = 1 ]; then printf 'other-app\n'; else printf 'amnezia-agent-app\n'; fi ;;
                *workload*) printf '%s\n' "$workload" ;;
                *) return 91 ;;
            esac ;;
        'container rm') present=0; printf 'REMOVE-CONTAINER\n' >&3 ;;
        'volume ls') [ "$docker_fails" = 0 ] || return 92; printf '%s\n' "$volume_list" ;;
        'volume rm')
            [ "$deletion_fails" = 0 ] || return 93
            printf 'REMOVE-VOLUME:%s\n' "$3" >&3
            if [ "$volume_remains" = 0 ]; then
                volume_list=$(printf '%s\n' "$volume_list" | awk -v removed="$3" '$0 != removed')
            fi ;;
        'network ls') [ "$network_present" = 0 ] || printf 'amnezia-agent-workloads\n'; return 0 ;;
        'network rm') network_present=0; printf 'REMOVE-NETWORK\n' >&3 ;;
        'image ls') [ "$image_present" = 0 ] || printf 'image-id\n'; return 0 ;;
        'image rm') image_present=0; printf 'REMOVE-IMAGE\n' >&3 ;;
        *) return 91 ;;
    esac
}
)");
    for (const auto &spec : { testAuthProxySpec(), testOpenClawSpec() }) {
        auto runner = std::make_unique<FakeCommandRunner>();
        auto *fake = runner.get();
        fake->commandOutcomes = {
            { ErrorCode::NoError, QString::fromUtf8(equivalentContainerObservation(spec)), {} },
            { ErrorCode::NoError, QStringLiteral("AMNEZIA_AGENT_REMOVE_COMPLETE\n"), {} },
            { ErrorCode::NoError, QString::fromUtf8(missingContainerObservation(spec)), {} },
        };
        SshSession session(nullptr, std::move(runner));
        QCOMPARE(session.changeAgentWorkloadLifecycle(testCredentials(), spec, AgentWorkloadLifecycleAction::Remove).status,
                 AgentWorkloadLifecycleStatus::Applied);
        const auto cleanup = decodedApplyPayload(fake->commands.at(1));
        QVERIFY(!cleanup.isEmpty());
        QVERIFY(fake->commands.at(1).contains(QStringLiteral("timeout 45s sh")));
        QStringList volumes;
        for (const auto &volume : spec.volumes) volumes.append(volume.name);
        const QStringList legacyVolumes { "amnezia-agent-runtime-openclaw-state", "amnezia-agent-runtime-workspace" };
        volumes.append(legacyVolumes);
        const auto sibling = spec.workload == QStringLiteral("amgpt-device-gateway")
                ? testOpenClawSpec() : testAuthProxySpec();
        QStringList siblingPrivateVolumes;
        for (const auto &volume : sibling.volumes) {
            if (!volumes.contains(volume.name)) {
                volumes.append(volume.name);
                siblingPrivateVolumes.append(volume.name);
            }
        }
        for (const auto &scenario : scenarios) {
            const auto setup = QStringLiteral(
                    "present=%1; shared_in_use=%2; deletion_fails=%3; volume_remains=%4; "
                    "foreign_owner=%5; docker_fails=%6; network_present=1; image_present=1; volume_list='%7';\n")
                                       .arg(scenario.present).arg(scenario.sharedInUse).arg(scenario.deletionFails)
                                       .arg(scenario.volumeRemains).arg(scenario.foreignOwner).arg(scenario.dockerFails)
                                       .arg(volumes.join(QLatin1Char('\n')));
            const bool succeeds = !scenario.deletionFails && !scenario.volumeRemains
                    && !scenario.foreignOwner && !scenario.dockerFails;
            QProcess shell;
            // Running the real playbook twice also proves retry after complete removal.
            shell.start(QStringLiteral("/bin/sh"), { QStringLiteral("-c"), setup + fakeDocker + cleanup
                                                    + (succeeds ? cleanup : QString()) });
            QVERIFY2(shell.waitForFinished(5000), scenario.name);
            QCOMPARE(shell.exitCode() == 0, succeeds);
            const auto output = shell.readAllStandardOutput();
            QCOMPARE(output.count("AMNEZIA_AGENT_REMOVE_COMPLETE"), succeeds ? 2 : 0);
            if (succeeds) {
                for (const auto &volume : spec.volumes) {
                    const bool shared = volume.name == QStringLiteral("amnezia-agent-codex-app-socket");
                    const auto marker = "REMOVE-VOLUME:" + volume.name.toUtf8() + '\n';
                    QCOMPARE(output.count(marker), shared && scenario.sharedInUse ? 0 : 1);
                }
                for (const auto &volume : legacyVolumes) {
                    QCOMPARE(output.count("REMOVE-VOLUME:" + volume.toUtf8() + '\n'),
                             spec.workload == QStringLiteral("openclaw-codex") ? 1 : 0);
                }
                QCOMPARE(output.count("REMOVE-NETWORK"), scenario.sharedInUse ? 0 : 1);
                QCOMPARE(output.count("REMOVE-IMAGE"), scenario.sharedInUse ? 0 : 1);
            }
            if (scenario.foreignOwner) {
                QVERIFY(!output.contains("REMOVE-"));
            }
            for (const auto &volume : siblingPrivateVolumes) {
                QVERIFY(!output.contains("REMOVE-VOLUME:" + volume.toUtf8() + '\n'));
            }
        }
    }
#endif
}

void SshSessionTest::agentWorkloadRemovalRequiresCleanupConfirmation()
{
    const auto spec = testAuthProxySpec();
    const QStringList replies { {}, QStringLiteral("unexpected\n"),
        QStringLiteral("AMNEZIA_AGENT_REMOVE_COMPLETE\nextra\n"), QString(1025, QLatin1Char('x')) };
    for (const auto &reply : replies) {
        auto runner = std::make_unique<FakeCommandRunner>();
        auto *fake = runner.get();
        fake->commandOutcomes = {
            { ErrorCode::NoError, QString::fromUtf8(missingContainerObservation(spec)), {} },
            { ErrorCode::NoError, reply, {} },
        };
        SshSession session(nullptr, std::move(runner));
        const auto result = session.changeAgentWorkloadLifecycle(testCredentials(), spec,
                                                                 AgentWorkloadLifecycleAction::Remove);
        QCOMPARE(result.status, AgentWorkloadLifecycleStatus::Unknown);
        QCOMPARE(fake->commands.size(), 2);
    }
}

void SshSessionTest::agentWorkloadLifecycleRejectsConflictAndClassifiesUnknown()
{
    const auto spec = testOpenClawSpec();
    {
        auto runner = std::make_unique<FakeCommandRunner>();
        auto *fake = runner.get();
        fake->commandOutcomes = {
            { ErrorCode::NoError, QString::fromUtf8(unmanagedContainerObservation(spec)), {} },
        };
        SshSession session(nullptr, std::move(runner));

        const auto result =
                session.changeAgentWorkloadLifecycle(testCredentials(), spec, AgentWorkloadLifecycleAction::Remove);

        QCOMPARE(result.status, AgentWorkloadLifecycleStatus::Conflict);
        QCOMPARE(result.reason, AgentWorkloadLifecycleReason::ContainerOwnedByOther);
        QCOMPARE(fake->commands.size(), 1);
    }
    {
        auto runner = std::make_unique<FakeCommandRunner>();
        auto *fake = runner.get();
        fake->commandOutcomes = {
            { ErrorCode::NoError, QString::fromUtf8(equivalentContainerObservation(spec)), {} },
            { ErrorCode::SshTimeoutError, {}, {} },
        };
        SshSession session(nullptr, std::move(runner));

        const auto result =
                session.changeAgentWorkloadLifecycle(testCredentials(), spec, AgentWorkloadLifecycleAction::Stop);

        QCOMPARE(result.status, AgentWorkloadLifecycleStatus::Unknown);
        QCOMPARE(result.reason, AgentWorkloadLifecycleReason::CommandFailed);
        QCOMPARE(fake->commands.size(), 2);
    }
    {
        auto runner = std::make_unique<FakeCommandRunner>();
        auto *fake = runner.get();
        fake->commandOutcomes = {
            { ErrorCode::NoError, QString::fromUtf8(missingContainerObservation(spec)), {} },
            { ErrorCode::NoError, QStringLiteral("AMNEZIA_AGENT_REMOVE_COMPLETE\n"), {} },
            { ErrorCode::NoError, QString::fromUtf8(missingContainerObservation(spec)), {} },
        };
        SshSession session(nullptr, std::move(runner));

        const auto result =
                session.changeAgentWorkloadLifecycle(testCredentials(), spec, AgentWorkloadLifecycleAction::Remove);

        QCOMPARE(result.status, AgentWorkloadLifecycleStatus::Applied);
        QCOMPARE(fake->commands.size(), 3);
        QVERIFY(decodedApplyPayload(fake->commands.at(1)).contains(QStringLiteral("docker volume rm")));
    }
}

void SshSessionTest::agentWorkloadRemovalRejectsInvalidTargetWithoutSsh()
{
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    SshSession session(nullptr, std::move(runner));
    QCOMPARE(session.removeAgentWorkload(testCredentials(), DockerContainer::OpenVpn).status,
             AgentWorkloadLifecycleStatus::Failed);
    auto target = testAuthProxySpec();
    target.containerName = QStringLiteral("amnezia-amgpt-device-gateway'; touch /tmp/injection; '");
    QCOMPARE(session.changeAgentWorkloadLifecycle(testCredentials(), target, AgentWorkloadLifecycleAction::Remove).status,
             AgentWorkloadLifecycleStatus::Failed);
    target = testAuthProxySpec();
    target.hostPort = QStringLiteral("8000");
    QCOMPARE(session.changeAgentWorkloadLifecycle(testCredentials(), target, AgentWorkloadLifecycleAction::Remove).status,
             AgentWorkloadLifecycleStatus::Failed);
    target = testAuthProxySpec();
    QCOMPARE(session.changeAgentWorkloadLifecycle(testCredentials(), target, static_cast<AgentWorkloadLifecycleAction>(99)).status,
             AgentWorkloadLifecycleStatus::Failed);
    QVERIFY(fake->commands.isEmpty());
}

void SshSessionTest::agentLoginStartUsesOwnedHealthyContainerAndExactCommand()
{
    const auto spec = testAuthProxySpec();
    const QString presentation = QString::fromLatin1(
            R"({"schema_version":1,"mode":"amgpt","state":"pending","verification_uri":"https://auth.example/device","verification_uri_complete":null,"user_code":"ABCD-EFGH","expires_in":null,"error_code":null})");
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    fake->commandOutcomes = {
        { ErrorCode::NoError, QString::fromUtf8(equivalentContainerObservation(spec)), {} },
        { ErrorCode::NoError, presentation, QStringLiteral("untrusted diagnostic") },
    };
    SshSession session(nullptr, std::move(runner));

    const auto result = session.startAgentWorkloadLogin(testCredentials(), spec, AgentWorkloadLoginMode::Amgpt);

    QCOMPARE(result.error, AgentWorkloadLoginOperationError::None);
    QVERIFY(result.presentation);
    QCOMPARE(result.presentation->userCode, QStringLiteral("ABCD-EFGH"));
    QCOMPARE(fake->commands.size(), 2);
    QCOMPARE(fake->commands.at(1),
             QStringLiteral("timeout 95s sudo -n docker exec -i amnezia-amgpt-device-gateway workloadctl login start amgpt --json"));
}

void SshSessionTest::agentLoginRejectsUnhealthyContainerBeforeExec()
{
    const auto spec = testAuthProxySpec();
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    fake->commandOutcomes = {
        { ErrorCode::NoError, QString::fromUtf8(pendingHealthObservation(spec)), {} },
    };
    SshSession session(nullptr, std::move(runner));

    const auto result = session.startAgentWorkloadLogin(testCredentials(), spec, AgentWorkloadLoginMode::Amgpt);

    QCOMPARE(result.error, AgentWorkloadLoginOperationError::WorkloadNotReady);
    QCOMPARE(fake->commands.size(), 1);
}

void SshSessionTest::agentLoginBoundsOutputAndParsesStatus()
{
    const auto spec = testAuthProxySpec();
    {
        auto runner = std::make_unique<FakeCommandRunner>();
        auto *fake = runner.get();
        fake->commandOutcomes = {
            { ErrorCode::NoError, QString::fromUtf8(equivalentContainerObservation(spec)), {} },
            { ErrorCode::NoError, QString(AgentWorkloadLoginMaxBytes + 1, QLatin1Char('x')), {} },
        };
        SshSession session(nullptr, std::move(runner));

        const auto result = session.startAgentWorkloadLogin(testCredentials(), spec, AgentWorkloadLoginMode::Amgpt);

        QCOMPARE(result.error, AgentWorkloadLoginOperationError::OutputTooLarge);
        QVERIFY(!result.presentation);
    }
    {
        const QString status = QString::fromLatin1(
                R"({"schema_version":1,"mode":"amgpt","state":"login-required","authenticated":false,"error_code":null})");
        auto runner = std::make_unique<FakeCommandRunner>();
        auto *fake = runner.get();
        fake->commandOutcomes = {
            { ErrorCode::NoError, QString::fromUtf8(equivalentContainerObservation(spec)), {} },
            { ErrorCode::NoError, status, {} },
        };
        SshSession session(nullptr, std::move(runner));

        const auto result = session.queryAgentWorkloadLoginStatus(testCredentials(), spec,
                                                                  AgentWorkloadLoginMode::Amgpt);

        QCOMPARE(result.error, AgentWorkloadLoginOperationError::None);
        QVERIFY(result.status);
        QCOMPARE(result.status->state, AgentWorkloadLoginState::LoginRequired);
        QCOMPARE(fake->commands.at(1),
                 QStringLiteral("timeout 10s sudo -n docker exec -i amnezia-amgpt-device-gateway workloadctl login status amgpt --json"));
    }
}

void SshSessionTest::enrollmentRecoversAfterFailure()
{
    const auto spec = testAuthProxySpec();
    const QString ready = QStringLiteral(R"({"schema_version":1,"mode":"amgpt","state":"ready","authenticated":true,"error_code":null})");
    const QString enrolled = QStringLiteral(R"({"state":"enrolled","key_thumbprint":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"})");
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    fake->commandOutcomes = {
        { ErrorCode::NoError, QString::fromUtf8(equivalentContainerObservation(spec)), {} },
        { ErrorCode::NoError, ready, {} },
        { ErrorCode::ServerCheckFailed, {}, {} },
        { ErrorCode::NoError, QString::fromUtf8(equivalentContainerObservation(spec)), {} },
        { ErrorCode::NoError, ready, {} },
        { ErrorCode::NoError, enrolled, {} },
        { ErrorCode::NoError, enrolled, {} },
    };
    SshSession session(nullptr, std::move(runner));
    QCOMPARE(session.queryAgentWorkloadLoginStatus(testCredentials(), spec, AgentWorkloadLoginMode::Amgpt).error,
             AgentWorkloadLoginOperationError::IngressEnrollmentFailed);
    const auto recovered = session.queryAgentWorkloadLoginStatus(testCredentials(), spec, AgentWorkloadLoginMode::Amgpt);
    QCOMPARE(recovered.error, AgentWorkloadLoginOperationError::None);
    QCOMPARE(recovered.status->state, AgentWorkloadLoginState::Ready);
    QCOMPARE(fake->commands.size(), 7);
    for (const auto &command : fake->commands) QVERIFY(!command.contains("login start"));
}

void SshSessionTest::agentLoginReadyRequiresEnrolledRecipient()
{
    const auto spec = testAuthProxySpec();
    const QString ready = QStringLiteral(
            R"({"schema_version":1,"mode":"amgpt","state":"ready","authenticated":true,"error_code":null})");
    const QString enrolled = QStringLiteral(
            R"({"state":"enrolled","key_thumbprint":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"})");
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    fake->commandOutcomes = {
        { ErrorCode::NoError, QString::fromUtf8(equivalentContainerObservation(spec)), {} },
        { ErrorCode::NoError, ready, {} },
        { ErrorCode::NoError, enrolled, {} },
        { ErrorCode::NoError, enrolled, {} },
    };
    SshSession session(nullptr, std::move(runner));

    const auto result = session.queryAgentWorkloadLoginStatus(testCredentials(), spec, AgentWorkloadLoginMode::Amgpt);

    QCOMPARE(result.error, AgentWorkloadLoginOperationError::None);
    QVERIFY(result.status);
    QCOMPARE(result.status->state, AgentWorkloadLoginState::Ready);
    QCOMPARE(fake->commands.size(), 4);
    QCOMPARE(fake->commands.at(2), QStringLiteral("timeout 25s sudo -n docker exec -i amnezia-amgpt-device-gateway "
                                                "amgpt-device-gateway manage ingress-init"));
    QCOMPARE(fake->commands.at(3), QStringLiteral("timeout 10s sudo -n docker exec -i amnezia-amgpt-device-gateway "
                                                "amgpt-device-gateway manage ingress-status"));
}

void SshSessionTest::agentLoginRejectsUnconfirmedRecipient_data()
{
    QTest::addColumn<int>("failedStage");
    QTest::addColumn<QString>("payload");
    QTest::addColumn<int>("transportError");
    const QList<QPair<QByteArray, QString>> invalid {
        { "not-enrolled", QStringLiteral(R"({"state":"uninitialized"})") },
        { "pending", QStringLiteral(R"({"state":"pending_enrollment"})") },
        { "auth-conflict", QStringLiteral(R"({"error":"key_conflict"})") },
        { "missing-thumbprint", QStringLiteral(R"({"state":"enrolled"})") },
        { "invalid-thumbprint", QStringLiteral(R"({"state":"enrolled","key_thumbprint":"invalid"})") },
        { "noncanonical-thumbprint", QStringLiteral(R"({"state":"enrolled","key_thumbprint":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAB"})") },
        { "wrong-type", QStringLiteral(R"({"state":"enrolled","key_thumbprint":true})") },
        { "unknown-field", QStringLiteral(R"({"state":"enrolled","key_thumbprint":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA","extra":"untrusted"})") },
        { "invalid-json", QStringLiteral("not-json") },
        { "trailing-output", QStringLiteral(R"({"state":"enrolled","key_thumbprint":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"} {})") },
        { "oversized", QString(1025, QLatin1Char('x')) },
    };
    for (const auto &[name, payload] : invalid) {
        QTest::newRow(("init-" + name).constData()) << 1 << payload << int(ErrorCode::NoError);
        QTest::newRow(("status-" + name).constData()) << 2 << payload << int(ErrorCode::NoError);
    }
    const QString enrolled = QStringLiteral(
            R"({"state":"enrolled","key_thumbprint":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"})");
    QTest::newRow("init-transport") << 1 << enrolled << int(ErrorCode::ServerCheckFailed);
    QTest::newRow("status-transport") << 2 << enrolled << int(ErrorCode::ServerCheckFailed);
    QTest::newRow("changed-recipient") << 2
            << QStringLiteral(R"({"state":"enrolled","key_thumbprint":"QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQ"})")
            << int(ErrorCode::NoError);
}

void SshSessionTest::agentLoginRejectsUnconfirmedRecipient()
{
    QFETCH(int, failedStage);
    QFETCH(QString, payload);
    QFETCH(int, transportError);
    const auto spec = testAuthProxySpec();
    const QString ready = QStringLiteral(
            R"({"schema_version":1,"mode":"amgpt","state":"ready","authenticated":true,"error_code":null})");
    const QString enrolled = QStringLiteral(
            R"({"state":"enrolled","key_thumbprint":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"})");
    auto runner = std::make_unique<FakeCommandRunner>();
    auto *fake = runner.get();
    fake->commandOutcomes = {
        { ErrorCode::NoError, QString::fromUtf8(equivalentContainerObservation(spec)), {} },
        { ErrorCode::NoError, ready, {} },
        { failedStage == 1 ? ErrorCode(transportError) : ErrorCode::NoError,
          failedStage == 1 ? payload : enrolled, QStringLiteral("untrusted diagnostic") },
        { failedStage == 2 ? ErrorCode(transportError) : ErrorCode::NoError,
          failedStage == 2 ? payload : enrolled, QStringLiteral("untrusted diagnostic") },
    };
    SshSession session(nullptr, std::move(runner));

    const auto result = session.queryAgentWorkloadLoginStatus(testCredentials(), spec, AgentWorkloadLoginMode::Amgpt);

    QCOMPARE(result.error, AgentWorkloadLoginOperationError::IngressEnrollmentFailed);
    QCOMPARE(result.transportError, ErrorCode(transportError));
    QVERIFY(!result.status);
    QCOMPARE(fake->commands.size(), failedStage + 2);
}

void SshSessionTest::agentLoginDoesNotEnrollBeforeApproval()
{
    const auto spec = testAuthProxySpec();
    for (const QString &state : { QStringLiteral("login-required"), QStringLiteral("pending"), QStringLiteral("denied") }) {
        auto runner = std::make_unique<FakeCommandRunner>();
        auto *fake = runner.get();
        const QString status = QString::fromUtf8(QJsonDocument(QJsonObject {
                { "schema_version", 1 }, { "mode", "amgpt" }, { "state", state },
                { "authenticated", false }, { "error_code", QJsonValue::Null },
        }).toJson(QJsonDocument::Compact));
        fake->commandOutcomes = {
            { ErrorCode::NoError, QString::fromUtf8(equivalentContainerObservation(spec)), {} },
            { ErrorCode::NoError, status, {} },
        };
        SshSession session(nullptr, std::move(runner));

        const auto result = session.queryAgentWorkloadLoginStatus(testCredentials(), spec, AgentWorkloadLoginMode::Amgpt);

        QCOMPARE(result.error, AgentWorkloadLoginOperationError::None);
        QVERIFY(result.status);
        QVERIFY(!result.status->authenticated);
        QCOMPARE(fake->commands.size(), 2);
    }
}

QTEST_GUILESS_MAIN(SshSessionTest)
#include "ssh_session_test.moc"
