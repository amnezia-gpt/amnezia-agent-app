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
        const auto spec = makeAgentWorkloadDeploymentSpec(config);
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
                                        { QStringLiteral("destination"), volume.target } });
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
    void agentWorkloadApplyRendererRejectsUnsafeInputAndActions();
    void agentWorkloadApplyReturnsNoOpWithoutMutation();
    void agentWorkloadApplyCreatesAndVerifies();
    void agentWorkloadApplyRunsStartAndRecreatePlans();
    void agentWorkloadApplyRejectsStaleOrUnsafePlan();
    void agentWorkloadApplyClassifiesKnownAndUnknownFailures();
    void agentWorkloadLifecycleStopsOnlyOwnedTarget();
    void agentWorkloadLifecycleRemovesOnlyOwnedTarget();
    void agentWorkloadLifecycleRejectsConflictAndClassifiesUnknown();
    void agentLoginStartUsesOwnedHealthyContainerAndExactCommand();
    void agentLoginRejectsUnhealthyContainerBeforeExec();
    void agentLoginBoundsOutputAndParsesStatus();
};

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
    QVERIFY(!command.contains(QStringLiteral(".Config.Env")));
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
    QVERIFY(script->contains(QStringLiteral("--cap-add 'SETUID'")));
    QVERIFY(script->contains(QStringLiteral("--cap-add 'SETGID'")));
    QVERIFY(script->contains(QStringLiteral("--cap-add 'KILL'")));
    QVERIFY(script->contains(QStringLiteral("--volume 'amnezia-agent-codex-app-socket:/run/amgpt-codex'")));
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
        { ErrorCode::NoError, {}, {} },
        { ErrorCode::NoError, QString::fromUtf8(missingContainerObservation(spec)), {} },
    };
    SshSession session(nullptr, std::move(runner));

    const auto result =
            session.changeAgentWorkloadLifecycle(testCredentials(), spec, AgentWorkloadLifecycleAction::Remove);

    QCOMPARE(result.status, AgentWorkloadLifecycleStatus::Applied);
    QCOMPARE(fake->commands.size(), 3);
    QVERIFY(fake->commands.at(1).contains(QStringLiteral("docker container rm --force")));
    QVERIFY(fake->commands.at(1).contains(spec.containerName));
    QVERIFY(!fake->commands.at(1).contains(QStringLiteral("docker volume")));
    QVERIFY(!fake->commands.at(1).contains(QStringLiteral("docker network")));
    QVERIFY(!fake->commands.at(1).contains(QStringLiteral("amnezia-openclaw-codex")));
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
        };
        SshSession session(nullptr, std::move(runner));

        const auto result =
                session.changeAgentWorkloadLifecycle(testCredentials(), spec, AgentWorkloadLifecycleAction::Remove);

        QCOMPARE(result.status, AgentWorkloadLifecycleStatus::NoOp);
        QCOMPARE(result.reason, AgentWorkloadLifecycleReason::ContainerMissing);
        QCOMPARE(fake->commands.size(), 1);
    }
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

QTEST_GUILESS_MAIN(SshSessionTest)
#include "ssh_session_test.moc"
