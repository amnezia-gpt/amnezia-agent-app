#ifndef SSHSESSION_H
#define SSHSESSION_H

#include <QJsonObject>
#include <QObject>

#include <functional>
#include <memory>

#include "core/models/agentWorkloadApply.h"
#include "core/models/agentWorkloadLogin.h"
#include "core/models/agentWorkloadReconciliation.h"
#include "core/utils/commonStructs.h"
#include "core/utils/containerEnum.h"
#include "core/utils/containers/containerUtils.h"
#include "core/utils/errorCodes.h"
#include "core/utils/protocolEnum.h"
#include "core/utils/routeModes.h"
#include "core/utils/selfhosted/sshClient.h"

using namespace amnezia;

// Narrow transport seam used by SshSession. Production uses the adapter below
// to delegate to libssh::Client; tests can provide a deterministic runner
// without opening a socket or changing deployment command construction.
class ISshCommandRunner
{
public:
    using OutputCallback = std::function<ErrorCode(const QString &, libssh::Client &)>;

    virtual ~ISshCommandRunner() = default;

    virtual ErrorCode connectToHost(const ServerCredentials &credentials) = 0;
    virtual void disconnectFromHost() = 0;
    virtual ErrorCode executeCommand(const QString &data, const OutputCallback &cbReadStdOut,
                                     const OutputCallback &cbReadStdErr) = 0;
    virtual ErrorCode scpFileCopy(libssh::ScpOverwriteMode overwriteMode, const QString &localPath,
                                  const QString &remotePath, const QString &fileDesc) = 0;
    virtual ErrorCode getDecryptedPrivateKey(const ServerCredentials &credentials, QString &decryptedPrivateKey,
                                             const std::function<QString()> &passphraseCallback) = 0;
};

class SshSession : public QObject
{
    Q_OBJECT
public:
    SshSession(QObject *parent = nullptr);
    SshSession(QObject *parent, std::unique_ptr<ISshCommandRunner> commandRunner);
    ~SshSession();

    typedef QList<QPair<QString, QString>> Vars;

    ErrorCode
    uploadTextFileToContainer(DockerContainer container, const ServerCredentials &credentials, const QString &file,
                              const QString &path,
                              libssh::ScpOverwriteMode overwriteMode = libssh::ScpOverwriteMode::ScpOverwriteExisting);
    QByteArray getTextFileFromContainer(DockerContainer container, const ServerCredentials &credentials,
                                        const QString &path, ErrorCode &errorCode);

    static QString replaceVars(const QString &script, const Vars &vars);

    ErrorCode runScript(const ServerCredentials &credentials, QString script,
                        const std::function<ErrorCode(const QString &, libssh::Client &)> &cbReadStdOut = nullptr,
                        const std::function<ErrorCode(const QString &, libssh::Client &)> &cbReadStdErr = nullptr);

    ErrorCode
    runContainerScript(const ServerCredentials &credentials, DockerContainer container, QString script,
                       const std::function<ErrorCode(const QString &, libssh::Client &)> &cbReadStdOut = nullptr,
                       const std::function<ErrorCode(const QString &, libssh::Client &)> &cbReadStdErr = nullptr);

    QString checkSshConnection(const ServerCredentials &credentials, ErrorCode &errorCode);

    AgentWorkloadObservationResult observeAgentWorkload(const ServerCredentials &credentials,
                                                        const AgentWorkloadDeploymentSpec &desired);

    AgentWorkloadApplyResult applyAgentWorkloadPlan(const ServerCredentials &credentials,
                                                    const AgentWorkloadDeploymentSpec &desired,
                                                    const AgentWorkloadReconciliationPlan &requestedPlan);

    AgentWorkloadLifecycleResult changeAgentWorkloadLifecycle(const ServerCredentials &credentials,
                                                              const AgentWorkloadDeploymentSpec &desired,
                                                              AgentWorkloadLifecycleAction action);

    AgentWorkloadLoginStartResult startAgentWorkloadLogin(const ServerCredentials &credentials,
                                                          const AgentWorkloadDeploymentSpec &desired,
                                                          AgentWorkloadLoginMode mode);

    AgentWorkloadLoginStatusResult queryAgentWorkloadLoginStatus(const ServerCredentials &credentials,
                                                                 const AgentWorkloadDeploymentSpec &desired,
                                                                 AgentWorkloadLoginMode mode);

    ErrorCode getDecryptedPrivateKey(const ServerCredentials &credentials, QString &decryptedPrivateKey,
                                     const std::function<QString()> &callback);

    ErrorCode uploadFileToHost(const ServerCredentials &credentials, const QByteArray &data, const QString &remotePath,
                               libssh::ScpOverwriteMode overwriteMode = libssh::ScpOverwriteMode::ScpOverwriteExisting);

private:
    std::unique_ptr<ISshCommandRunner> m_commandRunner;
};

#endif // SSHSESSION_H
