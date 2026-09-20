#include "sshSession.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPointer>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>
#include <QtConcurrent>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>

#include <chrono>
#include <thread>

#include "core/utils/containerEnum.h"
#include "core/utils/containers/containerUtils.h"
#include "core/utils/networkUtilities.h"
#include "core/utils/protocolEnum.h"
#include "core/utils/selfhosted/scriptsRegistry.h"
#include "core/utils/utilities.h"
#include "logger.h"

namespace
{
    Logger logger("SshSession");

    bool isSupportedObservationTarget(const AgentWorkloadDeploymentSpec &desired)
    {
        const bool knownIdentity = (desired.workload == QStringLiteral("amgpt-auth-proxy")
                                    && desired.containerName == QStringLiteral("amnezia-amgpt-auth-proxy"))
                || (desired.workload == QStringLiteral("openclaw-codex")
                    && desired.containerName == QStringLiteral("amnezia-openclaw-codex"));
        bool converted = false;
        const int port = desired.hostPort.toInt(&converted);
        return knownIdentity && converted && port >= 1 && port <= 65535 && desired.hostPort == QString::number(port);
    }

    bool isOpenClawLoginTarget(const AgentWorkloadDeploymentSpec &desired)
    {
        return desired.workload == QStringLiteral("openclaw-codex")
                && desired.containerName == QStringLiteral("amnezia-openclaw-codex")
                && isSupportedObservationTarget(desired);
    }

    QString agentWorkloadLoginCommand(AgentWorkloadLoginMode mode, bool status)
    {
        return QStringLiteral("timeout %1s sudo -n docker exec -i amnezia-openclaw-codex workloadctl login %2 %3 --json")
                .arg(status ? 10 : 25)
                .arg(status ? QStringLiteral("status") : QStringLiteral("start"), agentWorkloadLoginModeName(mode));
    }

    QString agentWorkloadObservationCommand(const AgentWorkloadDeploymentSpec &desired)
    {
        const QString inspectFormat = QStringLiteral(
                R"({"id":{{json .Id}},"name":{{json .Name}},"image_reference":{{json .Config.Image}},"image_id":{{json .Image}},"status":{{json .State.Status}},"running":{{json .State.Running}},"health":{{with .State.Health}}{{json .Status}}{{else}}"none"{{end}},"restart_policy":{{json .HostConfig.RestartPolicy.Name}},"mounts":[{{$first := true}}{{range .Mounts}}{{if not $first}},{{end}}{"type":{{json .Type}},"name":{{json .Name}},"destination":{{json .Destination}}}{{$first = false}}{{end}}],"tmpfs":[{{$first := true}}{{range $destination, $options := .HostConfig.Tmpfs}}{{if not $first}},{{end}}{"destination":{{json $destination}},"options":{{json $options}}}{{$first = false}}{{end}}],"networks":[{{$first := true}}{{range $name, $_ := .NetworkSettings.Networks}}{{if not $first}},{{end}}{{json $name}}{{$first = false}}{{end}}],"ports":[{{$first := true}}{{range $key, $bindings := .NetworkSettings.Ports}}{{range $binding := $bindings}}{{if not $first}},{{end}}{{$parts := split $key "/"}}{"host_ip":{{json $binding.HostIp}},"host_port":{{json $binding.HostPort}},"container_port":{{json (index $parts 0)}},"protocol":{{json (index $parts 1)}}}{{$first = false}}{{end}}{{end}}],"labels":{"org.amnezia.amgpt.deployment.managed-by":{{with index .Config.Labels "org.amnezia.amgpt.deployment.managed-by"}}{{json .}}{{else}}""{{end}},"org.amnezia.amgpt.deployment.workload":{{with index .Config.Labels "org.amnezia.amgpt.deployment.workload"}}{{json .}}{{else}}""{{end}},"org.amnezia.amgpt.deployment.schema-version":{{with index .Config.Labels "org.amnezia.amgpt.deployment.schema-version"}}{{json .}}{{else}}""{{end}},"org.amnezia.amgpt.deployment.workload-version":{{with index .Config.Labels "org.amnezia.amgpt.deployment.workload-version"}}{{json .}}{{else}}""{{end}},"org.amnezia.amgpt.deployment.spec-hash":{{with index .Config.Labels "org.amnezia.amgpt.deployment.spec-hash"}}{{json .}}{{else}}""{{end}}{{with index .Config.Labels "org.amnezia.amgpt.deployment.backend-profile"}},"org.amnezia.amgpt.deployment.backend-profile":{{json .}}{{end}}}})");

        return QStringLiteral(
                       "name='%1'; workload='%2'; port='%3'; command -v docker >/dev/null 2>&1 || exit 40; "
                       "command -v lsof >/dev/null 2>&1 || exit 41; sudo -n true >/dev/null 2>&1 || exit 42; "
                       "ids=$(sudo -n docker container ls -aq --no-trunc --filter \"name=^/${name}$\") || exit 43; "
                       "count=$(printf '%s\\n' \"$ids\" | awk 'NF { count++ } END { print count + 0 }'); "
                       "[ \"$count\" -le 1 ] || exit 44; container=null; "
                       "if [ -n \"$ids\" ]; then container=$(sudo -n docker container inspect "
                       "--format '%4' \"$ids\") || exit 45; fi; "
                       "port_lines=$(sudo -n docker container ls -a --no-trunc --filter \"publish=${port}/tcp\" "
                       "--format '{\"id\":{{json .ID}},\"name\":{{json .Names}}}') || exit 46; "
                       "port_owners=$(printf '%s\\n' \"$port_lines\" | awk 'BEGIN { printf \"[\" } NF { if (seen++) "
                       "printf \",\"; printf \"%s\", $0 } END { print \"]\" }') || exit 47; "
                       "host_port_in_use=false; lsof_output=$(sudo -n lsof -nP -iTCP:\"$port\" -sTCP:LISTEN "
                       "2>/dev/null); lsof_status=$?; "
                       "if [ \"$lsof_status\" -eq 0 ]; then host_port_in_use=true; elif [ \"$lsof_status\" -ne 1 ]; "
                       "then exit 48; fi; "
                       "printf "
                       "'{\"schema_version\":1,\"target\":{\"workload\":\"%s\",\"container_name\":\"%s\",\"host_port\":"
                       "\"%s\"},\"container\":%s,\"host_port_in_use\":%s,\"port_owners\":%s}\\n' "
                       "\"$workload\" \"$name\" \"$port\" \"$container\" \"$host_port_in_use\" \"$port_owners\"")
                .arg(desired.containerName, desired.workload, desired.hostPort, inspectFormat);
    }

    class LibsshCommandRunner final : public ISshCommandRunner
    {
    public:
        ErrorCode connectToHost(const ServerCredentials &credentials) override
        {
            return m_client.connectToHost(credentials);
        }

        void disconnectFromHost() override
        {
            m_client.disconnectFromHost();
        }

        ErrorCode executeCommand(const QString &data, const OutputCallback &cbReadStdOut,
                                 const OutputCallback &cbReadStdErr) override
        {
            return m_client.executeCommand(data, cbReadStdOut, cbReadStdErr);
        }

        ErrorCode scpFileCopy(libssh::ScpOverwriteMode overwriteMode, const QString &localPath,
                              const QString &remotePath, const QString &fileDesc) override
        {
            return m_client.scpFileCopy(overwriteMode, localPath, remotePath, fileDesc);
        }

        ErrorCode getDecryptedPrivateKey(const ServerCredentials &credentials, QString &decryptedPrivateKey,
                                         const std::function<QString()> &passphraseCallback) override
        {
            return m_client.getDecryptedPrivateKey(credentials, decryptedPrivateKey, passphraseCallback);
        }

    private:
        libssh::Client m_client;
    };
}

SshSession::SshSession(QObject *parent) : SshSession(parent, std::make_unique<LibsshCommandRunner>())
{
}

SshSession::SshSession(QObject *parent, std::unique_ptr<ISshCommandRunner> commandRunner)
    : QObject(parent),
      m_commandRunner(commandRunner ? std::move(commandRunner) : std::make_unique<LibsshCommandRunner>())
{
}

SshSession::~SshSession()
{
    m_commandRunner->disconnectFromHost();
}

ErrorCode SshSession::runScript(const ServerCredentials &credentials, QString script,
                                const std::function<ErrorCode(const QString &, libssh::Client &)> &cbReadStdOut,
                                const std::function<ErrorCode(const QString &, libssh::Client &)> &cbReadStdErr)
{

    auto error = m_commandRunner->connectToHost(credentials);
    if (error != ErrorCode::NoError) {
        return error;
    }

    script.replace("\r", "");

    qDebug() << "SshSession::Run script";

    QString totalLine;
    const QStringList &lines = script.split("\n", Qt::SkipEmptyParts);
    for (int i = 0; i < lines.count(); i++) {
        QString currentLine = lines.at(i);

        if (totalLine.isEmpty()) {
            totalLine = currentLine;
        } else {
            totalLine = totalLine + "\n" + currentLine;
        }

        QString lineToExec;
        if (currentLine.endsWith("\\")) {
            continue;
        } else {
            lineToExec = totalLine;
            totalLine.clear();
        }

        if (lineToExec.startsWith("#")) {
            continue;
        }

        qDebug() << "Executing remote deployment command";

        error = m_commandRunner->executeCommand(lineToExec, cbReadStdOut, cbReadStdErr);
        if (error != ErrorCode::NoError) {
            return error;
        }
    }

    qDebug().noquote() << "SshSession::runScript finished\n";
    return ErrorCode::NoError;
}

ErrorCode SshSession::runContainerScript(const ServerCredentials &credentials, DockerContainer container, QString script,
                                         const std::function<ErrorCode(const QString &, libssh::Client &)> &cbReadStdOut,
                                         const std::function<ErrorCode(const QString &, libssh::Client &)> &cbReadStdErr)
{
    QString fileName = "/opt/amnezia/" + Utils::getRandomString(16) + ".sh";

    ErrorCode e = uploadTextFileToContainer(container, credentials, script, fileName);
    if (e)
        return e;

    const bool useSh = container == DockerContainer::Socks5Proxy || container == DockerContainer::MtProxy
            || container == DockerContainer::Telemt;
    QString runner = QString("sudo docker exec -i $CONTAINER_NAME %2 %1 ").arg(fileName, useSh ? "sh" : "bash");
    e = runScript(credentials, replaceVars(runner, amnezia::genBaseVars(credentials, container, QString(), QString())),
                  cbReadStdOut, cbReadStdErr);

    QString remover = QString("sudo docker exec -i $CONTAINER_NAME rm %1 ").arg(fileName);
    runScript(credentials, replaceVars(remover, amnezia::genBaseVars(credentials, container, QString(), QString())),
              cbReadStdOut, cbReadStdErr);

    return e;
}

ErrorCode SshSession::uploadTextFileToContainer(DockerContainer container, const ServerCredentials &credentials,
                                                const QString &file, const QString &path,
                                                libssh::ScpOverwriteMode overwriteMode)
{
    ErrorCode e = ErrorCode::NoError;
    QString tmpFileName = QString("/tmp/%1.tmp").arg(Utils::getRandomString(16));
    e = uploadFileToHost(credentials, file.toUtf8(), tmpFileName);
    if (e)
        return e;

    QString stdOut;
    auto cbReadStd = [&](const QString &data, libssh::Client &) {
        stdOut += data + "\n";
        return ErrorCode::NoError;
    };

    // mkdir
    QString mkdir = QString("sudo docker exec -i $CONTAINER_NAME mkdir -p  \"$(dirname %1)\"").arg(path);

    e = runScript(credentials, replaceVars(mkdir, amnezia::genBaseVars(credentials, container, QString(), QString())));
    if (e)
        return e;

    if (overwriteMode == libssh::ScpOverwriteMode::ScpOverwriteExisting) {
        e = runScript(credentials,
                      replaceVars(QStringLiteral("sudo docker cp %1 $CONTAINER_NAME:/%2").arg(tmpFileName, path),
                                  amnezia::genBaseVars(credentials, container, QString(), QString())),
                      cbReadStd, cbReadStd);

        if (e)
            return e;
    } else if (overwriteMode == libssh::ScpOverwriteMode::ScpAppendToExisting) {
        e = runScript(credentials,
                      replaceVars(QStringLiteral("sudo docker cp %1 $CONTAINER_NAME:/%2").arg(tmpFileName, tmpFileName),
                                  amnezia::genBaseVars(credentials, container, QString(), QString())),
                      cbReadStd, cbReadStd);

        if (e)
            return e;

        e = runScript(credentials,
                      replaceVars(QStringLiteral("sudo docker exec -i $CONTAINER_NAME sh -c \"cat %1 >> %2\"")
                                          .arg(tmpFileName, path),
                                  amnezia::genBaseVars(credentials, container, QString(), QString())),
                      cbReadStd, cbReadStd);

        if (e)
            return e;
    } else
        return ErrorCode::NotImplementedError;

    if (stdOut.contains("Error") && stdOut.contains("No such container")) {
        return ErrorCode::ServerContainerMissingError;
    }

    runScript(credentials,
              replaceVars(QString("sudo shred -u %1").arg(tmpFileName),
                          amnezia::genBaseVars(credentials, container, QString(), QString())));
    return e;
}

QByteArray SshSession::getTextFileFromContainer(DockerContainer container, const ServerCredentials &credentials,
                                                const QString &path, ErrorCode &errorCode)
{

    errorCode = ErrorCode::NoError;

    QString script = QStringLiteral("sudo docker exec -i %1 sh -c \"xxd -p '%2' 2>/dev/null || od -An -v -tx1 '%2'\"")
                             .arg(ContainerUtils::containerToString(container), path);

    QString stdOut;
    auto cbReadStdOut = [&](const QString &data, libssh::Client &) {
        stdOut += data;
        return ErrorCode::NoError;
    };

    errorCode = runScript(credentials, script, cbReadStdOut);
    return QByteArray::fromHex(stdOut.toUtf8());
}

ErrorCode SshSession::uploadFileToHost(const ServerCredentials &credentials, const QByteArray &data,
                                       const QString &remotePath, libssh::ScpOverwriteMode overwriteMode)
{
    auto error = m_commandRunner->connectToHost(credentials);
    if (error != ErrorCode::NoError) {
        return error;
    }

    QTemporaryFile localFile;
    localFile.open();
    localFile.write(data);
    localFile.close();

    error = m_commandRunner->scpFileCopy(overwriteMode, localFile.fileName(), remotePath, "non_desc");

    if (error != ErrorCode::NoError) {
        return error;
    }
    return ErrorCode::NoError;
}

QString SshSession::checkSshConnection(const ServerCredentials &credentials, ErrorCode &errorCode)
{
    QString stdOut;
    auto cbReadStdOut = [&](const QString &data, libssh::Client &) {
        stdOut += data + "\n";
        return ErrorCode::NoError;
    };
    auto cbReadStdErr = [&](const QString &data, libssh::Client &) {
        stdOut += data + "\n";
        return ErrorCode::NoError;
    };

    errorCode =
            runScript(credentials, amnezia::scriptData(SharedScriptType::check_connection), cbReadStdOut, cbReadStdErr);

    return stdOut;
}

AgentWorkloadObservationResult SshSession::observeAgentWorkload(const ServerCredentials &credentials,
                                                                const AgentWorkloadDeploymentSpec &desired)
{
    if (!isSupportedObservationTarget(desired)) {
        return failedAgentWorkloadObservation(AgentWorkloadObservationError::TargetMismatch);
    }

    QByteArray output;
    bool outputTooLarge = false;
    const auto stdoutCallback = [&output, &outputTooLarge](const QString &data, libssh::Client &) {
        const QByteArray chunk = data.toUtf8();
        if (chunk.size() > AgentWorkloadObservationMaxBytes - output.size()) {
            outputTooLarge = true;
            return ErrorCode::ServerCheckFailed;
        }
        output.append(chunk);
        return ErrorCode::NoError;
    };
    const auto discardStderr = [](const QString &, libssh::Client &) { return ErrorCode::NoError; };

    const ErrorCode error =
            runScript(credentials, agentWorkloadObservationCommand(desired), stdoutCallback, discardStderr);
    if (outputTooLarge) {
        return failedAgentWorkloadObservation(AgentWorkloadObservationError::OutputTooLarge);
    }
    if (error != ErrorCode::NoError) {
        return failedAgentWorkloadObservation(AgentWorkloadObservationError::CommandFailed);
    }
    return parseAgentWorkloadObservation(output, desired);
}

AgentWorkloadApplyResult SshSession::applyAgentWorkloadPlan(const ServerCredentials &credentials,
                                                            const AgentWorkloadDeploymentSpec &desired,
                                                            const AgentWorkloadReconciliationPlan &requestedPlan)
{
    using Action = AgentWorkloadReconciliationAction;
    using Reason = AgentWorkloadApplyReason;
    using Status = AgentWorkloadApplyStatus;

    if (validateAgentWorkloadDeploymentSpec(desired) != AgentDeploymentValidationError::None) {
        return { Status::Failed, Reason::InvalidDesiredState };
    }
    if (requestedPlan.action == Action::Conflict || requestedPlan.action == Action::Unknown) {
        return { Status::Failed, Reason::UnsupportedPlan };
    }

    const AgentWorkloadObservationResult preflight = observeAgentWorkload(credentials, desired);
    const AgentWorkloadReconciliationPlan freshPlan = planAgentWorkloadReconciliation(desired, preflight);
    if (freshPlan.action == Action::Conflict || freshPlan.action == Action::Unknown) {
        return { Status::Failed, Reason::PreflightObservationFailed };
    }
    if (freshPlan.action == Action::NoOp) {
        return { Status::NoOp, Reason::None };
    }
    if (freshPlan.action != requestedPlan.action) {
        return { Status::Failed, Reason::StalePlan };
    }

    AgentWorkloadApplyRenderError renderError = AgentWorkloadApplyRenderError::None;
    const auto script = renderAgentWorkloadApplyScript(desired, freshPlan.action, &renderError);
    if (!script) {
        return { Status::Failed, Reason::RenderFailed };
    }

    QByteArray output;
    bool outputTooLarge = false;
    const auto stdoutCallback = [&output, &outputTooLarge](const QString &data, libssh::Client &) {
        const QByteArray chunk = data.toUtf8();
        if (chunk.size() > AgentWorkloadApplyOutputMaxBytes - output.size()) {
            outputTooLarge = true;
            return ErrorCode::ServerCheckFailed;
        }
        output.append(chunk);
        return ErrorCode::NoError;
    };
    const auto discardStderr = [](const QString &, libssh::Client &) { return ErrorCode::NoError; };
    const ErrorCode commandError =
            runScript(credentials, agentWorkloadApplyCommand(*script), stdoutCallback, discardStderr);
    if (outputTooLarge) {
        return { Status::Unknown, Reason::OutputTooLarge };
    }
    if (commandError != ErrorCode::NoError) {
        return { Status::Unknown, Reason::CommandFailed, commandError };
    }

    bool applied = false;
    bool failed = false;
    bool healthTimeout = false;
    const QList<QByteArray> lines = output.split('\n');
    for (QByteArray line : lines) {
        line = line.trimmed();
        applied = applied || line == QByteArray(AgentWorkloadApplySuccessMarker);
        failed = failed || line.startsWith(QByteArray(AgentWorkloadApplyFailureMarker) + ':');
        healthTimeout = healthTimeout || line == QByteArray(AgentWorkloadApplyHealthTimeoutMarker);
    }
    if (healthTimeout) {
        return { Status::Failed, Reason::HealthTimeout };
    }
    if (failed) {
        return { Status::Failed, Reason::ScriptFailed };
    }
    if (!applied) {
        return { Status::Unknown, Reason::MissingTerminalMarker };
    }

    const AgentWorkloadObservationResult postApply = observeAgentWorkload(credentials, desired);
    if (postApply.error != AgentWorkloadObservationError::None || !postApply.state) {
        return { Status::Unknown, Reason::PostApplyObservationFailed };
    }
    const AgentWorkloadReconciliationPlan postApplyPlan = planAgentWorkloadReconciliation(desired, postApply);
    if (postApplyPlan.action == Action::Unknown) {
        return { Status::Unknown, Reason::PostApplyObservationFailed };
    }
    if (postApplyPlan.action != Action::NoOp) {
        return { Status::Failed, Reason::PostApplyDrift };
    }
    return { Status::Applied, Reason::None };
}

AgentWorkloadLifecycleResult SshSession::changeAgentWorkloadLifecycle(const ServerCredentials &credentials,
                                                                      const AgentWorkloadDeploymentSpec &desired,
                                                                      AgentWorkloadLifecycleAction action)
{
    using Reason = AgentWorkloadLifecycleReason;
    using Status = AgentWorkloadLifecycleStatus;

    if (validateAgentWorkloadDeploymentSpec(desired) != AgentDeploymentValidationError::None) {
        return { Status::Failed, Reason::InvalidDesiredState };
    }

    const AgentWorkloadObservationResult before = observeAgentWorkload(credentials, desired);
    if (before.error != AgentWorkloadObservationError::None || !before.state) {
        return { Status::Unknown, Reason::ObservationFailed };
    }
    if (!before.state->containerPresent) {
        return { Status::NoOp, Reason::ContainerMissing };
    }

    const QMap<QString, QString> expectedLabels = desired.deploymentLabels();
    const QString managedByKey = QStringLiteral("org.amnezia.amgpt.deployment.managed-by");
    const QString workloadKey = QStringLiteral("org.amnezia.amgpt.deployment.workload");
    if (before.state->deploymentLabels.value(managedByKey) != expectedLabels.value(managedByKey)
        || before.state->deploymentLabels.value(workloadKey) != desired.workload) {
        return { Status::Conflict, Reason::ContainerOwnedByOther };
    }
    if (action == AgentWorkloadLifecycleAction::Stop && !before.state->running) {
        return { Status::NoOp, Reason::AlreadyStopped };
    }

    QString command;
    if (action == AgentWorkloadLifecycleAction::Stop) {
        command = QStringLiteral("sudo -n docker container stop --time %1 '%2' >/dev/null 2>&1")
                          .arg(desired.stopGracePeriodSeconds)
                          .arg(desired.containerName);
    } else {
        command = QStringLiteral("sudo -n docker container rm --force '%1' >/dev/null 2>&1").arg(desired.containerName);
    }

    const ErrorCode commandError = runScript(credentials, command);
    if (commandError != ErrorCode::NoError) {
        return { Status::Unknown, Reason::CommandFailed, commandError };
    }

    const AgentWorkloadObservationResult after = observeAgentWorkload(credentials, desired);
    if (after.error != AgentWorkloadObservationError::None || !after.state) {
        return { Status::Unknown, Reason::PostMutationObservationFailed };
    }
    const bool expectedState = action == AgentWorkloadLifecycleAction::Stop
            ? after.state->containerPresent && !after.state->running
            : !after.state->containerPresent;
    if (!expectedState) {
        return { Status::Failed, Reason::PostMutationMismatch };
    }
    return { Status::Applied, Reason::None };
}

AgentWorkloadLoginStartResult SshSession::startAgentWorkloadLogin(
        const ServerCredentials &credentials, const AgentWorkloadDeploymentSpec &openClawDesired,
        AgentWorkloadLoginMode mode)
{
    AgentWorkloadLoginStartResult result;
    if (!isOpenClawLoginTarget(openClawDesired)) {
        return result;
    }
    const AgentWorkloadObservationResult observation = observeAgentWorkload(credentials, openClawDesired);
    if (observation.error != AgentWorkloadObservationError::None) {
        result.error = AgentWorkloadLoginOperationError::ObservationFailed;
        return result;
    }
    if (planAgentWorkloadReconciliation(openClawDesired, observation).action
        != AgentWorkloadReconciliationAction::NoOp) {
        result.error = AgentWorkloadLoginOperationError::WorkloadNotReady;
        return result;
    }

    QByteArray output;
    bool outputTooLarge = false;
    const auto capture = [&output, &outputTooLarge](const QString &data, libssh::Client &) {
        const QByteArray chunk = data.toUtf8();
        if (chunk.size() > AgentWorkloadLoginMaxBytes - output.size()) {
            outputTooLarge = true;
            return ErrorCode::ServerCheckFailed;
        }
        output.append(chunk);
        return ErrorCode::NoError;
    };
    const auto discard = [](const QString &, libssh::Client &) { return ErrorCode::NoError; };
    const ErrorCode commandError = runScript(credentials, agentWorkloadLoginCommand(mode, false), capture, discard);
    if (outputTooLarge) {
        result.error = AgentWorkloadLoginOperationError::OutputTooLarge;
        return result;
    }
    if (commandError != ErrorCode::NoError) {
        result.error = AgentWorkloadLoginOperationError::CommandFailed;
        result.transportError = commandError;
        return result;
    }
    const AgentWorkloadLoginStartParseResult parsed = parseAgentWorkloadLoginStart(output, mode);
    result.parseError = parsed.error;
    result.remoteError = parsed.remoteError;
    result.presentation = parsed.presentation;
    if (parsed.error == AgentWorkloadLoginParseError::None) {
        result.error = AgentWorkloadLoginOperationError::None;
    } else if (parsed.error == AgentWorkloadLoginParseError::RemoteFailure) {
        result.error = AgentWorkloadLoginOperationError::RemoteFailure;
    } else {
        result.error = AgentWorkloadLoginOperationError::InvalidResponse;
    }
    return result;
}

AgentWorkloadLoginStatusResult SshSession::queryAgentWorkloadLoginStatus(
        const ServerCredentials &credentials, const AgentWorkloadDeploymentSpec &openClawDesired,
        AgentWorkloadLoginMode mode)
{
    AgentWorkloadLoginStatusResult result;
    if (!isOpenClawLoginTarget(openClawDesired)) {
        return result;
    }
    const AgentWorkloadObservationResult observation = observeAgentWorkload(credentials, openClawDesired);
    if (observation.error != AgentWorkloadObservationError::None) {
        result.error = AgentWorkloadLoginOperationError::ObservationFailed;
        return result;
    }
    if (planAgentWorkloadReconciliation(openClawDesired, observation).action
        != AgentWorkloadReconciliationAction::NoOp) {
        result.error = AgentWorkloadLoginOperationError::WorkloadNotReady;
        return result;
    }

    QByteArray output;
    bool outputTooLarge = false;
    const auto capture = [&output, &outputTooLarge](const QString &data, libssh::Client &) {
        const QByteArray chunk = data.toUtf8();
        if (chunk.size() > AgentWorkloadLoginMaxBytes - output.size()) {
            outputTooLarge = true;
            return ErrorCode::ServerCheckFailed;
        }
        output.append(chunk);
        return ErrorCode::NoError;
    };
    const auto discard = [](const QString &, libssh::Client &) { return ErrorCode::NoError; };
    const ErrorCode commandError = runScript(credentials, agentWorkloadLoginCommand(mode, true), capture, discard);
    if (outputTooLarge) {
        result.error = AgentWorkloadLoginOperationError::OutputTooLarge;
        return result;
    }
    if (commandError != ErrorCode::NoError) {
        result.error = AgentWorkloadLoginOperationError::CommandFailed;
        result.transportError = commandError;
        return result;
    }
    const AgentWorkloadLoginStatusParseResult parsed = parseAgentWorkloadLoginStatus(output, mode);
    result.parseError = parsed.error;
    result.status = parsed.status;
    result.error = parsed.error == AgentWorkloadLoginParseError::None
            ? AgentWorkloadLoginOperationError::None
            : AgentWorkloadLoginOperationError::InvalidResponse;
    return result;
}

QString SshSession::replaceVars(const QString &script, const Vars &vars)
{
    QString s = script;
    for (const QPair<QString, QString> &var : vars) {
        s.replace(var.first, var.second);
    }
    return s;
}

ErrorCode SshSession::getDecryptedPrivateKey(const ServerCredentials &credentials, QString &decryptedPrivateKey,
                                             const std::function<QString()> &callback)
{
    auto error = m_commandRunner->getDecryptedPrivateKey(credentials, decryptedPrivateKey, callback);
    return error;
}
