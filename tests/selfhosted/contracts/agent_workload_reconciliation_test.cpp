#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/models/agentWorkloadReconciliation.h"
#include "core/models/protocols/amgptAuthProxyProtocolConfig.h"
#include "core/models/protocols/openClawCodexProtocolConfig.h"

using namespace amnezia;

namespace
{
    AgentWorkloadDeploymentSpec authProxySpec(const QString &profile = QStringLiteral("development"))
    {
        AmgptAuthProxyProtocolConfig config;
        config.backendProfile = profile;
        config.authIssuer = profile == QStringLiteral("development") ? QStringLiteral("https://auth-dev.example.com")
                                                                     : QStringLiteral("https://auth.example.com");
        config.routerBaseUrl = profile == QStringLiteral("development")
                ? QStringLiteral("https://router-dev.example.com/v1")
                : QStringLiteral("https://router.example.com/v1");
        config.runtimeGatewayBaseUrl = QStringLiteral("https://runtime.example.com");
        const auto spec = makeAgentWorkloadDeploymentSpec(config);
        Q_ASSERT(spec);
        return *spec;
    }

    AgentWorkloadDeploymentSpec openClawSpec()
    {
        OpenClawCodexProtocolConfig config;
        const auto spec = makeAgentWorkloadDeploymentSpec(config);
        Q_ASSERT(spec);
        return *spec;
    }

    // Current workloads publish nothing; publication matching stays covered for a future listener.
    AgentWorkloadDeploymentSpec publishedSpec(AgentWorkloadDeploymentSpec spec)
    {
        spec.hostPort = QStringLiteral("18443");
        spec.containerPort = QStringLiteral("8443");
        return spec;
    }

    QJsonObject observedContainer(const AgentWorkloadDeploymentSpec &spec, bool running = true,
                                  const QString &health = QStringLiteral("healthy"))
    {
        QJsonArray mounts;
        for (const AgentWorkloadVolume &volume : spec.volumes) {
            mounts.append(QJsonObject { { QStringLiteral("type"), QStringLiteral("volume") },
                                        { QStringLiteral("name"), volume.name },
                                        { QStringLiteral("destination"), volume.target } });
        }

        QJsonObject labelObject;
        const auto desiredLabels = spec.deploymentLabels();
        for (auto it = desiredLabels.cbegin(); it != desiredLabels.cend(); ++it) {
            labelObject.insert(it.key(), it.value());
        }

        QJsonArray tmpfs;
        for (const QString &entry : spec.tmpfs) {
            const qsizetype separator = entry.indexOf(QLatin1Char(':'));
            tmpfs.append(QJsonObject { { QStringLiteral("destination"), entry.left(separator) },
                                       { QStringLiteral("options"), entry.mid(separator + 1) } });
        }

        return {
            { QStringLiteral("id"), QStringLiteral("container-id") },
            { QStringLiteral("name"), spec.containerName },
            { QStringLiteral("image_reference"), spec.imageReference },
            { QStringLiteral("image_id"), QStringLiteral("sha256:runtime-image") },
            { QStringLiteral("status"), running ? QStringLiteral("running") : QStringLiteral("exited") },
            { QStringLiteral("running"), running },
            { QStringLiteral("health"), health },
            { QStringLiteral("restart_policy"), spec.restartPolicy },
            { QStringLiteral("mounts"), mounts },
            { QStringLiteral("tmpfs"), tmpfs },
            { QStringLiteral("networks"), QJsonArray { spec.networkName } },
            { QStringLiteral("ports"),
              spec.hostPort.isEmpty()
                      ? QJsonArray {}
                      : QJsonArray { QJsonObject { { QStringLiteral("host_ip"), QStringLiteral("0.0.0.0") },
                                                   { QStringLiteral("host_port"), spec.hostPort },
                                                   { QStringLiteral("container_port"), spec.containerPort },
                                                   { QStringLiteral("protocol"), QStringLiteral("tcp") } } } },
            { QStringLiteral("labels"), labelObject },
        };
    }

    QByteArray envelope(const AgentWorkloadDeploymentSpec &spec, const QJsonValue &container,
                        bool hostPortInUse = false, const QJsonArray &portOwners = {})
    {
        return QJsonDocument(QJsonObject {
                                     { QStringLiteral("schema_version"), 1 },
                                     { QStringLiteral("target"),
                                       QJsonObject { { QStringLiteral("workload"), spec.workload },
                                                     { QStringLiteral("container_name"), spec.containerName },
                                                     { QStringLiteral("host_port"), spec.hostPort } } },
                                     { QStringLiteral("container"), container },
                                     { QStringLiteral("host_port_in_use"), hostPortInUse },
                                     { QStringLiteral("port_owners"), portOwners },
                             })
                .toJson(QJsonDocument::Compact);
    }

    AgentWorkloadReconciliationPlan parseAndPlan(const AgentWorkloadDeploymentSpec &spec, const QByteArray &payload)
    {
        return planAgentWorkloadReconciliation(spec, parseAgentWorkloadObservation(payload, spec));
    }
} // namespace

class AgentWorkloadReconciliationTest final : public QObject
{
    Q_OBJECT

private slots:
    void missingContainerPlansCreate();
    void equivalentRunningContainerPlansNoOp();
    void dualStackPublicationMatchesWithoutHidingDrift();
    void equivalentStoppedContainerPlansStart();
    void managedDriftPlansRecreate();
    void runtimeGatewayCoordinateDriftPlansRecreate();
    void runtimeDriftPlansRecreate();
    void unpublishedWorkloadRejectsPublishedPorts();
    void wrongOwnerPlansConflict();
    void occupiedPortPlansConflict();
    void invalidObservationPlansUnknown();
    void commandFailurePlansUnknown();
    void unhealthyAndStartingAreSafe();
    void workloadsRemainIndependent();
};

void AgentWorkloadReconciliationTest::missingContainerPlansCreate()
{
    const auto spec = authProxySpec();
    const auto plan = parseAndPlan(spec, envelope(spec, QJsonValue::Null));
    QCOMPARE(plan.action, AgentWorkloadReconciliationAction::Create);
    QCOMPARE(plan.reason, AgentWorkloadReconciliationReason::ContainerMissing);
}

void AgentWorkloadReconciliationTest::equivalentRunningContainerPlansNoOp()
{
    const auto spec = authProxySpec();
    const auto plan = parseAndPlan(spec, envelope(spec, observedContainer(spec)));
    QCOMPARE(plan.action, AgentWorkloadReconciliationAction::NoOp);
    QCOMPARE(plan.reason, AgentWorkloadReconciliationReason::EquivalentAndRunning);
}

void AgentWorkloadReconciliationTest::dualStackPublicationMatchesWithoutHidingDrift()
{
    for (const auto &spec : { publishedSpec(authProxySpec()), publishedSpec(openClawSpec()) }) {
        auto container = observedContainer(spec);
        auto ports = container.value(QStringLiteral("ports")).toArray();
        auto ipv6 = ports.at(0).toObject();
        ipv6.insert(QStringLiteral("host_ip"), QStringLiteral("::"));
        ports.append(ipv6);
        container.insert(QStringLiteral("ports"), ports);
        QCOMPARE(parseAndPlan(spec, envelope(spec, container)).action, AgentWorkloadReconciliationAction::NoOp);
        for (const auto &field : { QStringLiteral("host_port"), QStringLiteral("container_port"),
                                  QStringLiteral("protocol"), QStringLiteral("host_ip") }) {
            auto changed = ipv6;
            changed.insert(field, field == QStringLiteral("protocol") ? QStringLiteral("udp")
                                       : field == QStringLiteral("host_ip") ? QStringLiteral("127.0.0.1")
                                                                            : QStringLiteral("9999"));
            auto wrongPorts = ports;
            wrongPorts.replace(1, changed);
            container.insert(QStringLiteral("ports"), wrongPorts);
            QCOMPARE(parseAndPlan(spec, envelope(spec, container)).reason,
                     AgentWorkloadReconciliationReason::RuntimeConfigurationDrift);
        }
    }
}

void AgentWorkloadReconciliationTest::equivalentStoppedContainerPlansStart()
{
    const auto spec = openClawSpec();
    const QJsonArray owners { QJsonObject { { QStringLiteral("id"), QStringLiteral("container-id") },
                                            { QStringLiteral("name"), spec.containerName } } };
    const auto plan = parseAndPlan(spec, envelope(spec, observedContainer(spec, false), false, owners));
    QCOMPARE(plan.action, AgentWorkloadReconciliationAction::Start);
    QCOMPARE(plan.reason, AgentWorkloadReconciliationReason::EquivalentButStopped);
}

void AgentWorkloadReconciliationTest::managedDriftPlansRecreate()
{
    const auto spec = authProxySpec();

    QJsonObject wrongHash = observedContainer(spec);
    QJsonObject labels = wrongHash.value(QStringLiteral("labels")).toObject();
    labels.insert(QStringLiteral("org.amnezia.amgpt.deployment.spec-hash"), QStringLiteral("wrong"));
    wrongHash.insert(QStringLiteral("labels"), labels);
    QCOMPARE(parseAndPlan(spec, envelope(spec, wrongHash)).action, AgentWorkloadReconciliationAction::Recreate);

    QJsonObject wrongImage = observedContainer(spec);
    wrongImage.insert(QStringLiteral("image_reference"), QStringLiteral("docker.io/example/other:v1"));
    const auto imagePlan = parseAndPlan(spec, envelope(spec, wrongImage));
    QCOMPARE(imagePlan.action, AgentWorkloadReconciliationAction::Recreate);
    QCOMPARE(imagePlan.reason, AgentWorkloadReconciliationReason::ImageDrift);

    QJsonObject wrongProfile = observedContainer(spec);
    labels = wrongProfile.value(QStringLiteral("labels")).toObject();
    labels.insert(QStringLiteral("org.amnezia.amgpt.deployment.backend-profile"), QStringLiteral("production"));
    wrongProfile.insert(QStringLiteral("labels"), labels);
    const auto profilePlan = parseAndPlan(spec, envelope(spec, wrongProfile));
    QCOMPARE(profilePlan.action, AgentWorkloadReconciliationAction::Recreate);
    QCOMPARE(profilePlan.reason, AgentWorkloadReconciliationReason::DeclarationDrift);
}

void AgentWorkloadReconciliationTest::runtimeGatewayCoordinateDriftPlansRecreate()
{
    const auto installed = authProxySpec();
    AmgptAuthProxyProtocolConfig config;
    config.backendProfile = installed.backendProfile->id;
    config.authIssuer = installed.backendProfile->authIssuer;
    config.routerBaseUrl = installed.backendProfile->routerBaseUrl;
    config.runtimeGatewayBaseUrl = QStringLiteral("https://another-runtime.example.com");
    const auto desired = makeAgentWorkloadDeploymentSpec(config);
    QVERIFY(desired);
    const auto plan = parseAndPlan(*desired, envelope(*desired, observedContainer(installed)));
    QCOMPARE(plan.action, AgentWorkloadReconciliationAction::Recreate);
    QCOMPARE(plan.reason, AgentWorkloadReconciliationReason::DeclarationDrift);
    QCOMPARE(installed.volumes.at(0).name, desired->volumes.at(0).name);
}

void AgentWorkloadReconciliationTest::runtimeDriftPlansRecreate()
{
    const auto spec = openClawSpec();

    QJsonObject wrongRestart = observedContainer(spec);
    wrongRestart.insert(QStringLiteral("restart_policy"), QStringLiteral("no"));
    QCOMPARE(parseAndPlan(spec, envelope(spec, wrongRestart)).reason,
             AgentWorkloadReconciliationReason::RuntimeConfigurationDrift);

    QJsonObject wrongMount = observedContainer(spec);
    wrongMount.insert(QStringLiteral("mounts"), QJsonArray {});
    QCOMPARE(parseAndPlan(spec, envelope(spec, wrongMount)).action, AgentWorkloadReconciliationAction::Recreate);

    QJsonObject wrongTmpfs = observedContainer(spec);
    wrongTmpfs.insert(QStringLiteral("tmpfs"), QJsonArray {});
    QCOMPARE(parseAndPlan(spec, envelope(spec, wrongTmpfs)).action, AgentWorkloadReconciliationAction::Recreate);

    QJsonObject wrongNetwork = observedContainer(spec);
    wrongNetwork.insert(QStringLiteral("networks"), QJsonArray { QStringLiteral("bridge") });
    QCOMPARE(parseAndPlan(spec, envelope(spec, wrongNetwork)).action, AgentWorkloadReconciliationAction::Recreate);

    const auto published = publishedSpec(spec);
    QJsonObject wrongPort = observedContainer(published);
    QJsonArray ports = wrongPort.value(QStringLiteral("ports")).toArray();
    QJsonObject port = ports.at(0).toObject();
    port.insert(QStringLiteral("container_port"), QStringLiteral("9999"));
    ports.replace(0, port);
    wrongPort.insert(QStringLiteral("ports"), ports);
    QCOMPARE(parseAndPlan(published, envelope(published, wrongPort)).action,
             AgentWorkloadReconciliationAction::Recreate);
}

void AgentWorkloadReconciliationTest::unpublishedWorkloadRejectsPublishedPorts()
{
    for (const auto &spec : { authProxySpec(), openClawSpec() }) {
        QVERIFY(spec.hostPort.isEmpty());
        QCOMPARE(parseAndPlan(spec, envelope(spec, observedContainer(spec))).action,
                 AgentWorkloadReconciliationAction::NoOp);

        // A container left from a release that published /v1 or the OpenClaw port must be replaced.
        QJsonObject legacy = observedContainer(spec);
        legacy.insert(QStringLiteral("ports"),
                      QJsonArray { QJsonObject { { QStringLiteral("host_ip"), QStringLiteral("0.0.0.0") },
                                                 { QStringLiteral("host_port"), QStringLiteral("8080") },
                                                 { QStringLiteral("container_port"), QStringLiteral("8080") },
                                                 { QStringLiteral("protocol"), QStringLiteral("tcp") } } });
        const auto plan = parseAndPlan(spec, envelope(spec, legacy));
        QCOMPARE(plan.action, AgentWorkloadReconciliationAction::Recreate);
        QCOMPARE(plan.reason, AgentWorkloadReconciliationReason::RuntimeConfigurationDrift);
    }
}

void AgentWorkloadReconciliationTest::wrongOwnerPlansConflict()
{
    const auto spec = authProxySpec();
    QJsonObject container = observedContainer(spec);
    QJsonObject labels = container.value(QStringLiteral("labels")).toObject();
    labels.insert(QStringLiteral("org.amnezia.amgpt.deployment.managed-by"), QStringLiteral("somebody-else"));
    container.insert(QStringLiteral("labels"), labels);

    const auto plan = parseAndPlan(spec, envelope(spec, container));
    QCOMPARE(plan.action, AgentWorkloadReconciliationAction::Conflict);
    QCOMPARE(plan.reason, AgentWorkloadReconciliationReason::ContainerOwnedByOther);
}

void AgentWorkloadReconciliationTest::occupiedPortPlansConflict()
{
    const auto spec = publishedSpec(openClawSpec());
    const QJsonArray owners { QJsonObject { { QStringLiteral("id"), QStringLiteral("other-id") },
                                            { QStringLiteral("name"), QStringLiteral("other-container") } } };
    const auto plan = parseAndPlan(spec, envelope(spec, QJsonValue::Null, true, owners));
    QCOMPARE(plan.action, AgentWorkloadReconciliationAction::Conflict);
    QCOMPARE(plan.reason, AgentWorkloadReconciliationReason::HostPortInUse);

    QJsonObject targetWithoutDesiredPort = observedContainer(spec);
    targetWithoutDesiredPort.insert(QStringLiteral("ports"), QJsonArray {});
    const auto hostProcessPlan = parseAndPlan(spec, envelope(spec, targetWithoutDesiredPort, true));
    QCOMPARE(hostProcessPlan.action, AgentWorkloadReconciliationAction::Conflict);
    QCOMPARE(hostProcessPlan.reason, AgentWorkloadReconciliationReason::HostPortInUse);
}

void AgentWorkloadReconciliationTest::invalidObservationPlansUnknown()
{
    const auto spec = authProxySpec();
    const QList<QByteArray> invalidPayloads {
        QByteArrayLiteral("{truncated"),
        QJsonDocument(QJsonObject { { QStringLiteral("schema_version"), 2 } }).toJson(QJsonDocument::Compact),
        envelope(spec, QJsonObject { { QStringLiteral("id"), QStringLiteral("incomplete") } }),
    };

    for (const QByteArray &payload : invalidPayloads) {
        const auto result = parseAgentWorkloadObservation(payload, spec);
        QVERIFY(!result.state);
        QCOMPARE(planAgentWorkloadReconciliation(spec, result).action, AgentWorkloadReconciliationAction::Unknown);
    }

    const QByteArray oversized(AgentWorkloadObservationMaxBytes + 1, 'x');
    const auto oversizedResult = parseAgentWorkloadObservation(oversized, spec);
    QCOMPARE(oversizedResult.error, AgentWorkloadObservationError::OutputTooLarge);

    QJsonObject contradictory = observedContainer(spec);
    contradictory.insert(QStringLiteral("status"), QStringLiteral("exited"));
    contradictory.insert(QStringLiteral("running"), true);
    QCOMPARE(parseAndPlan(spec, envelope(spec, contradictory)).action, AgentWorkloadReconciliationAction::Unknown);

    QJsonObject unknownLabel = observedContainer(spec);
    QJsonObject labels = unknownLabel.value(QStringLiteral("labels")).toObject();
    labels.insert(QStringLiteral("org.amnezia.amgpt.deployment.unrecognized"), QStringLiteral("value"));
    unknownLabel.insert(QStringLiteral("labels"), labels);
    QCOMPARE(parseAndPlan(spec, envelope(spec, unknownLabel)).action, AgentWorkloadReconciliationAction::Unknown);
}

void AgentWorkloadReconciliationTest::commandFailurePlansUnknown()
{
    const auto spec = authProxySpec();
    const auto result = failedAgentWorkloadObservation(AgentWorkloadObservationError::CommandFailed);
    const auto plan = planAgentWorkloadReconciliation(spec, result);
    QCOMPARE(plan.action, AgentWorkloadReconciliationAction::Unknown);
    QCOMPARE(plan.reason, AgentWorkloadReconciliationReason::ObservationCommandFailed);
}

void AgentWorkloadReconciliationTest::unhealthyAndStartingAreSafe()
{
    const auto spec = openClawSpec();

    const auto unhealthy = parseAndPlan(spec, envelope(spec, observedContainer(spec, true, QStringLiteral("unhealthy"))));
    QCOMPARE(unhealthy.action, AgentWorkloadReconciliationAction::Recreate);
    QCOMPARE(unhealthy.reason, AgentWorkloadReconciliationReason::ContainerUnhealthy);

    const auto starting = parseAndPlan(spec, envelope(spec, observedContainer(spec, true, QStringLiteral("starting"))));
    QCOMPARE(starting.action, AgentWorkloadReconciliationAction::Unknown);
    QCOMPARE(starting.reason, AgentWorkloadReconciliationReason::ContainerHealthPending);

    QJsonObject dead = observedContainer(spec, false);
    dead.insert(QStringLiteral("status"), QStringLiteral("dead"));
    const auto deadPlan = parseAndPlan(spec, envelope(spec, dead));
    QCOMPARE(deadPlan.action, AgentWorkloadReconciliationAction::Unknown);
    QCOMPARE(deadPlan.reason, AgentWorkloadReconciliationReason::ContainerLifecycleIndeterminate);
}

void AgentWorkloadReconciliationTest::workloadsRemainIndependent()
{
    const auto developmentProxy = authProxySpec(QStringLiteral("development"));
    const auto productionProxy = authProxySpec(QStringLiteral("production"));
    const auto openClaw = openClawSpec();

    const QByteArray oldProxyObservation = envelope(developmentProxy, observedContainer(developmentProxy));
    QJsonObject retargeted = QJsonDocument::fromJson(oldProxyObservation).object();
    retargeted.insert(QStringLiteral("target"),
                      QJsonObject { { QStringLiteral("workload"), productionProxy.workload },
                                    { QStringLiteral("container_name"), productionProxy.containerName },
                                    { QStringLiteral("host_port"), productionProxy.hostPort } });
    QCOMPARE(parseAndPlan(productionProxy, QJsonDocument(retargeted).toJson(QJsonDocument::Compact)).action,
             AgentWorkloadReconciliationAction::Recreate);

    QCOMPARE(parseAndPlan(openClaw, envelope(openClaw, observedContainer(openClaw))).action,
             AgentWorkloadReconciliationAction::NoOp);
}

QTEST_GUILESS_MAIN(AgentWorkloadReconciliationTest)
#include "agent_workload_reconciliation_test.moc"
