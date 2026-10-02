#include <QtTest>

#include "core/models/agentWorkloadDeploymentSpec.h"
#include "core/models/protocols/amgptAuthProxyProtocolConfig.h"
#include "core/models/protocols/openClawCodexProtocolConfig.h"

using namespace amnezia;

namespace
{
    AgentBackendProfileCatalog completeCatalog()
    {
        return {
            { QStringLiteral("production"), QStringLiteral("https://auth.example.com"),
              QStringLiteral("https://router.example.com/v1"), QStringLiteral("https://runtime.example.com") },
            { QStringLiteral("development"), QStringLiteral("https://auth-dev.example.com"),
              QStringLiteral("https://router-dev.example.com/v1"), QStringLiteral("https://runtime-dev.example.com") },
        };
    }

    AmgptAuthProxyProtocolConfig authProxyConfig(const AgentBackendProfile &profile, const QString &port = {})
    {
        AmgptAuthProxyProtocolConfig config;
        config.port = port;
        config.backendProfile = profile.id;
        config.authIssuer = profile.authIssuer;
        config.routerBaseUrl = profile.routerBaseUrl;
        config.runtimeGatewayBaseUrl = profile.runtimeGatewayBaseUrl;
        return config;
    }
} // namespace

class AgentWorkloadDeploymentSpecTest : public QObject
{
    Q_OBJECT

private slots:
    void runtimeGatewayProfileReachesDeployment();
    void resolvesBackendProfilesAtomically();
    void rejectsIncompleteBackendProfiles();
    void rejectsUnknownBackendEnvironment();
    void persistsResolvedBackendProfile();
    void createsCompleteAuthProxyDesiredState();
    void keepsOpenClawIndependentFromBackendProfile();
    void createsSupervisedRuntimeDesiredState();
    void sharesOnlyTheAppSocketVolume();
    void ignoresPersistedPublishedPorts();
    void rejectsMalformedRuntimeUrls_data();
    void rejectsMalformedRuntimeUrls();
    void runtimeGatewayChangeChangesHash();
    void rejectsMalformedProfileUrls_data();
    void rejectsMalformedProfileUrls();
    void normalizesEquivalentProfilesBeforeHashing();
    void changesHashWhenDesiredStateChanges();
    void rendersOnlyWorkloadOwnedVariables();
    void validatesCompleteCanonicalSpec();
};

void AgentWorkloadDeploymentSpecTest::runtimeGatewayProfileReachesDeployment()
{
    const QJsonObject profile {
        { QStringLiteral("backend_profile"), QStringLiteral("local") },
        { QStringLiteral("auth_issuer"), QStringLiteral("https://auth-local.example.com") },
        { QStringLiteral("router_base_url"), QStringLiteral("https://router-local.example.com/v1") },
        { QStringLiteral("runtime_gateway_base_url"), QStringLiteral("https://runtime-local.example.com") },
    };
    const auto config = AmgptAuthProxyProtocolConfig::fromJson(profile);
    QCOMPARE(config.toJson().value(QStringLiteral("runtime_gateway_base_url")),
             profile.value(QStringLiteral("runtime_gateway_base_url")));
    const auto spec = makeAgentWorkloadDeploymentSpec(config);
    QVERIFY(spec);
    QCOMPARE(spec->environment.value(QStringLiteral("AMGPT_RUNTIME_GATEWAY_BASE_URL")),
             QStringLiteral("https://runtime-local.example.com"));
}

void AgentWorkloadDeploymentSpecTest::resolvesBackendProfilesAtomically()
{
    const AgentBackendProfileCatalog catalog = completeCatalog();

    const auto production = resolveAgentBackendProfile(AgentBackendEnvironment::Production, catalog);
    QVERIFY(production);
    QCOMPARE(production->id, QStringLiteral("production"));
    QCOMPARE(production->authIssuer, QStringLiteral("https://auth.example.com"));
    QCOMPARE(production->routerBaseUrl, QStringLiteral("https://router.example.com/v1"));

    const auto development = resolveAgentBackendProfile(AgentBackendEnvironment::Development, catalog);
    QVERIFY(development);
    QCOMPARE(development->id, QStringLiteral("development"));
    QCOMPARE(development->authIssuer, QStringLiteral("https://auth-dev.example.com"));
    QCOMPARE(development->routerBaseUrl, QStringLiteral("https://router-dev.example.com/v1"));
    QCOMPARE(development->runtimeGatewayBaseUrl, QStringLiteral("https://runtime-dev.example.com"));
}

void AgentWorkloadDeploymentSpecTest::rejectsIncompleteBackendProfiles()
{
    AgentBackendProfileCatalog catalog = completeCatalog();
    catalog.development.routerBaseUrl.clear();

    AgentDeploymentValidationError error = AgentDeploymentValidationError::None;
    const auto profile = resolveAgentBackendProfile(AgentBackendEnvironment::Development, catalog, &error);

    QVERIFY(!profile);
    QCOMPARE(error, AgentDeploymentValidationError::MissingBackendProfile);
}

void AgentWorkloadDeploymentSpecTest::rejectsUnknownBackendEnvironment()
{
    AgentDeploymentValidationError error = AgentDeploymentValidationError::None;
    const auto profile = resolveAgentBackendProfile(static_cast<AgentBackendEnvironment>(99), completeCatalog(), &error);

    QVERIFY(!profile);
    QCOMPARE(error, AgentDeploymentValidationError::UnsupportedEnvironment);
}

void AgentWorkloadDeploymentSpecTest::persistsResolvedBackendProfile()
{
    const auto profile = completeCatalog().development;
    const AmgptAuthProxyProtocolConfig original = authProxyConfig(profile, QStringLiteral("18080"));

    const AmgptAuthProxyProtocolConfig restored = AmgptAuthProxyProtocolConfig::fromJson(original.toJson());

    QCOMPARE(restored.port, original.port);
    QCOMPARE(restored.backendProfile, original.backendProfile);
    QCOMPARE(restored.authIssuer, original.authIssuer);
    QCOMPARE(restored.routerBaseUrl, original.routerBaseUrl);
    QCOMPARE(restored.runtimeGatewayBaseUrl, original.runtimeGatewayBaseUrl);
}

void AgentWorkloadDeploymentSpecTest::createsCompleteAuthProxyDesiredState()
{
    AgentDeploymentValidationError error = AgentDeploymentValidationError::None;
    const auto spec = makeAgentWorkloadDeploymentSpec(authProxyConfig(completeCatalog().production), &error);

    QVERIFY(spec);
    QCOMPARE(error, AgentDeploymentValidationError::None);
    QCOMPARE(spec->schemaVersion, 1);
    QCOMPARE(spec->workloadVersion,
             QStringLiteral("dev"));
    QCOMPARE(spec->imageReference,
             QStringLiteral("docker.io/amneziavpn/agent-workload-amgpt-auth-proxy:dev"));
    QCOMPARE(spec->platform, QStringLiteral("linux/amd64"));
    QCOMPARE(spec->containerName, QStringLiteral("amnezia-amgpt-device-gateway"));
    QCOMPARE(spec->networkAlias, QStringLiteral("amgpt-device-gateway"));
    QCOMPARE(spec->networkName, QStringLiteral("amnezia-agent-workloads"));
    // The private provider endpoint injects Router credentials; publishing it would expose compute.
    QVERIFY(spec->hostPort.isEmpty());
    QVERIFY(spec->containerPort.isEmpty());
    QCOMPARE(spec->restartPolicy, QStringLiteral("unless-stopped"));
    QCOMPARE(spec->environment.value(QStringLiteral("AMGPT_AUTH_ISSUER")), QStringLiteral("https://auth.example.com"));
    QCOMPARE(spec->environment.value(QStringLiteral("AMGPT_ROUTER_BASE_URL")),
             QStringLiteral("https://router.example.com/v1"));
    QVERIFY(!spec->environment.contains(QStringLiteral("AMGPT_PROXY_MANAGED_DEVICE")));
    QCOMPARE(spec->environment.value(QStringLiteral("AMGPT_DEVICE_GATEWAY_STATE_DIR")),
             QStringLiteral("/var/lib/amgpt-device-gateway"));
    QCOMPARE(spec->environment.value(QStringLiteral("AMGPT_INGRESS_STATE_DIR")),
             QStringLiteral("/var/lib/amgpt-device-gateway/ingress"));
    QCOMPARE(spec->environment.value(QStringLiteral("AMGPT_CODEX_APP_SOCKET")),
             QStringLiteral("/run/amgpt-codex/app-server.sock"));
    QCOMPARE(spec->environment.size(), 6);
    QCOMPARE(spec->volumes.size(), 2);
    QCOMPARE(spec->volumes.at(0).name, QStringLiteral("amnezia-amgpt-device-gateway-state"));
    QCOMPARE(spec->volumes.at(0).target, QStringLiteral("/var/lib/amgpt-device-gateway"));
    QCOMPARE(spec->volumes.at(1).name, QStringLiteral("amnezia-agent-codex-app-socket"));
    QCOMPARE(spec->volumes.at(1).target, QStringLiteral("/run/amgpt-codex"));
    QCOMPARE(spec->tmpfs, QStringList { QStringLiteral("/tmp:rw,noexec,nosuid,nodev") });
    QCOMPARE(spec->capabilitiesDropped, QStringList { QStringLiteral("ALL") });
    QVERIFY(spec->capabilitiesAdded.isEmpty());
    QCOMPARE(spec->securityOptions, QStringList { QStringLiteral("no-new-privileges") });
    QCOMPARE(spec->healthCheck.command,
             QStringList({ QStringLiteral("/usr/local/bin/amgpt-device-gateway"), QStringLiteral("healthcheck") }));

    const auto labels = spec->deploymentLabels();
    QCOMPARE(labels.size(), 6);
    QCOMPARE(labels.value(QStringLiteral("org.amnezia.amgpt.deployment.managed-by")),
             QStringLiteral("amnezia-agent-app"));
    QCOMPARE(labels.value(QStringLiteral("org.amnezia.amgpt.deployment.workload")),
             QStringLiteral("amgpt-device-gateway"));
    QCOMPARE(labels.value(QStringLiteral("org.amnezia.amgpt.deployment.schema-version")), QStringLiteral("1"));
    QCOMPARE(labels.value(QStringLiteral("org.amnezia.amgpt.deployment.workload-version")),
             QStringLiteral("dev"));
    QCOMPARE(labels.value(QStringLiteral("org.amnezia.amgpt.deployment.backend-profile")), QStringLiteral("production"));
    QCOMPARE(labels.value(QStringLiteral("org.amnezia.amgpt.deployment.spec-hash")), spec->specHash());
    QCOMPARE(spec->specHash().size(), 64);
}

void AgentWorkloadDeploymentSpecTest::keepsOpenClawIndependentFromBackendProfile()
{
    const auto spec = makeAgentWorkloadDeploymentSpec(OpenClawCodexProtocolConfig {});

    QVERIFY(spec);
    QVERIFY(!spec->backendProfile);
    QVERIFY(!spec->environment.contains(QStringLiteral("AMGPT_AUTH_ISSUER")));
    QVERIFY(!spec->environment.contains(QStringLiteral("AMGPT_ROUTER_BASE_URL")));
    QVERIFY(!spec->environment.contains(QStringLiteral("AMGPT_AUTH_PROXY_URL")));
    QVERIFY(!spec->deploymentLabels().contains(QStringLiteral("org.amnezia.amgpt.deployment.backend-profile")));
}

void AgentWorkloadDeploymentSpecTest::createsSupervisedRuntimeDesiredState()
{
    const auto spec = makeAgentWorkloadDeploymentSpec(OpenClawCodexProtocolConfig {});

    QVERIFY(spec);
    QCOMPARE(spec->workload, QStringLiteral("openclaw-codex"));
    QCOMPARE(spec->containerName, QStringLiteral("amnezia-openclaw-codex"));
    QVERIFY(spec->networkAlias.isEmpty());
    QVERIFY(spec->hostPort.isEmpty());
    QVERIFY(spec->containerPort.isEmpty());
    // The image owns HOME, CODEX_HOME and OpenClaw paths for each service identity.
    QVERIFY(spec->environment.isEmpty());

    const QList<AgentWorkloadVolume> expectedVolumes {
        { QStringLiteral("amnezia-agent-runtime-openclaw-state"), QStringLiteral("/home/openclaw/.openclaw") },
        { QStringLiteral("amnezia-agent-runtime-workspace"), QStringLiteral("/workspace") },
        { QStringLiteral("amnezia-agent-runtime-codex-home"), QStringLiteral("/home/codex/.codex") },
        { QStringLiteral("amnezia-agent-runtime-codex-workspace"), QStringLiteral("/codex-workspace") },
        { QStringLiteral("amnezia-agent-codex-app-socket"), QStringLiteral("/run/amgpt-codex") },
    };
    QCOMPARE(spec->volumes.size(), expectedVolumes.size());
    for (qsizetype index = 0; index < expectedVolumes.size(); ++index) {
        QCOMPARE(spec->volumes.at(index).name, expectedVolumes.at(index).name);
        QCOMPARE(spec->volumes.at(index).target, expectedVolumes.at(index).target);
    }
    QCOMPARE(spec->tmpfs, QStringList { QStringLiteral("/tmp:rw,noexec,nosuid,nodev") });
    QCOMPARE(spec->capabilitiesDropped, QStringList { QStringLiteral("ALL") });
    QCOMPARE(spec->capabilitiesAdded,
             QStringList({ QStringLiteral("SETUID"), QStringLiteral("SETGID"), QStringLiteral("KILL") }));
    QCOMPARE(spec->securityOptions, QStringList { QStringLiteral("no-new-privileges") });
    QCOMPARE(spec->healthCheck.command,
             QStringList({ QStringLiteral("node"), QStringLiteral("/opt/workload/bin/runtimectl.mjs"),
                           QStringLiteral("healthcheck") }));
    QCOMPARE(spec->healthCheck.startPeriodSeconds, 60);
    QCOMPARE(spec->stopGracePeriodSeconds, 20);
}

void AgentWorkloadDeploymentSpecTest::sharesOnlyTheAppSocketVolume()
{
    const auto gateway = makeAgentWorkloadDeploymentSpec(authProxyConfig(completeCatalog().development));
    const auto runtime = makeAgentWorkloadDeploymentSpec(OpenClawCodexProtocolConfig {});
    QVERIFY(gateway);
    QVERIFY(runtime);

    QSet<QString> gatewayVolumes;
    for (const auto &volume : gateway->volumes) {
        gatewayVolumes.insert(volume.name + QLatin1Char(':') + volume.target);
    }
    QSet<QString> shared;
    for (const auto &volume : runtime->volumes) {
        const QString mount = volume.name + QLatin1Char(':') + volume.target;
        if (gatewayVolumes.contains(mount)) {
            shared.insert(mount);
        }
    }
    QCOMPARE(shared, QSet<QString> { QStringLiteral("amnezia-agent-codex-app-socket:/run/amgpt-codex") });
    QCOMPARE(gateway->networkName, runtime->networkName);
}

void AgentWorkloadDeploymentSpecTest::ignoresPersistedPublishedPorts()
{
    const AmgptAuthProxyProtocolConfig gatewayConfig =
            authProxyConfig(completeCatalog().development, QStringLiteral("18080"));
    OpenClawCodexProtocolConfig runtimeConfig;
    runtimeConfig.port = QStringLiteral("28789");

    const auto gateway = makeAgentWorkloadDeploymentSpec(gatewayConfig);
    const auto runtime = makeAgentWorkloadDeploymentSpec(runtimeConfig);

    QVERIFY(gateway);
    QVERIFY(runtime);
    QVERIFY(gateway->hostPort.isEmpty());
    QVERIFY(runtime->hostPort.isEmpty());
    QCOMPARE(gateway->specHash(),
             makeAgentWorkloadDeploymentSpec(authProxyConfig(completeCatalog().development))->specHash());
}

void AgentWorkloadDeploymentSpecTest::rejectsMalformedRuntimeUrls_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<AgentDeploymentValidationError>("expectedError");
    QTest::newRow("missing") << QString() << AgentDeploymentValidationError::MissingBackendProfile;
    for (const auto &row : QList<QPair<const char *, QString>> {
             { "http", QStringLiteral("http://runtime.example.com") },
             { "relative", QStringLiteral("runtime.example.com") },
             { "userinfo", QStringLiteral("https://user@runtime.example.com") },
             { "query", QStringLiteral("https://runtime.example.com?tenant=dev") },
             { "fragment", QStringLiteral("https://runtime.example.com#fragment") },
             { "v1", QStringLiteral("https://runtime.example.com/v1") },
             { "v1-slash", QStringLiteral("https://runtime.example.com/v1/") },
             { "v1-slashes", QStringLiteral("https://runtime.example.com/v1//") },
             { "prefixed-v1", QStringLiteral("https://runtime.example.com/gateway/v1") },
             { "encoded-v1", QStringLiteral("https://runtime.example.com/%761") },
             { "whitespace", QStringLiteral(" https://runtime.example.com") },
         }) {
        QTest::newRow(row.first) << row.second << AgentDeploymentValidationError::InvalidRuntimeGatewayBaseUrl;
    }
}

void AgentWorkloadDeploymentSpecTest::rejectsMalformedRuntimeUrls()
{
    QFETCH(QString, url);
    QFETCH(AgentDeploymentValidationError, expectedError);
    auto profile = completeCatalog().development;
    profile.runtimeGatewayBaseUrl = url;
    AgentDeploymentValidationError error = AgentDeploymentValidationError::None;
    QVERIFY(!makeAgentWorkloadDeploymentSpec(authProxyConfig(profile), &error));
    QCOMPARE(error, expectedError);
}

void AgentWorkloadDeploymentSpecTest::runtimeGatewayChangeChangesHash()
{
    auto profile = completeCatalog().development;
    const auto first = makeAgentWorkloadDeploymentSpec(authProxyConfig(profile));
    profile.runtimeGatewayBaseUrl = QStringLiteral("https://another-runtime.example.com");
    const auto second = makeAgentWorkloadDeploymentSpec(authProxyConfig(profile));
    QVERIFY(first);
    QVERIFY(second);
    QVERIFY(first->specHash() != second->specHash());
    QCOMPARE(validateAgentWorkloadDeploymentSpec(*second), AgentDeploymentValidationError::None);
}

void AgentWorkloadDeploymentSpecTest::rejectsMalformedProfileUrls_data()
{
    QTest::addColumn<QString>("profileId");
    QTest::addColumn<QString>("authIssuer");
    QTest::addColumn<QString>("routerBaseUrl");
    QTest::addColumn<AgentDeploymentValidationError>("expectedError");

    QTest::newRow("empty-id") << QString() << QStringLiteral("https://auth.example.com")
                              << QStringLiteral("https://router.example.com/v1")
                              << AgentDeploymentValidationError::InvalidBackendProfileId;
    QTest::newRow("id-is-url") << QStringLiteral("https://profile.example") << QStringLiteral("https://auth.example.com")
                               << QStringLiteral("https://router.example.com/v1")
                               << AgentDeploymentValidationError::InvalidBackendProfileId;
    QTest::newRow("issuer-http") << QStringLiteral("development") << QStringLiteral("http://auth.example.com")
                                 << QStringLiteral("https://router.example.com/v1")
                                 << AgentDeploymentValidationError::InvalidAuthIssuer;
    QTest::newRow("issuer-userinfo") << QStringLiteral("development") << QStringLiteral("https://user@auth.example.com")
                                     << QStringLiteral("https://router.example.com/v1")
                                     << AgentDeploymentValidationError::InvalidAuthIssuer;
    QTest::newRow("issuer-query") << QStringLiteral("development")
                                  << QStringLiteral("https://auth.example.com?tenant=dev")
                                  << QStringLiteral("https://router.example.com/v1")
                                  << AgentDeploymentValidationError::InvalidAuthIssuer;
    QTest::newRow("router-http") << QStringLiteral("development") << QStringLiteral("https://auth.example.com")
                                 << QStringLiteral("http://router.example.com/v1")
                                 << AgentDeploymentValidationError::InvalidRouterBaseUrl;
    QTest::newRow("router-wrong-path") << QStringLiteral("development") << QStringLiteral("https://auth.example.com")
                                       << QStringLiteral("https://router.example.com/api")
                                       << AgentDeploymentValidationError::InvalidRouterBaseUrl;
    QTest::newRow("router-fragment") << QStringLiteral("development") << QStringLiteral("https://auth.example.com")
                                     << QStringLiteral("https://router.example.com/v1#fragment")
                                     << AgentDeploymentValidationError::InvalidRouterBaseUrl;
}

void AgentWorkloadDeploymentSpecTest::rejectsMalformedProfileUrls()
{
    QFETCH(QString, profileId);
    QFETCH(QString, authIssuer);
    QFETCH(QString, routerBaseUrl);
    QFETCH(AgentDeploymentValidationError, expectedError);

    AgentDeploymentValidationError error = AgentDeploymentValidationError::None;
    const auto spec = makeAgentWorkloadDeploymentSpec(
            authProxyConfig({ profileId, authIssuer, routerBaseUrl, QStringLiteral("https://runtime.example.com") }), &error);

    QVERIFY(!spec);
    QCOMPARE(error, expectedError);
}

void AgentWorkloadDeploymentSpecTest::normalizesEquivalentProfilesBeforeHashing()
{
    const auto first = makeAgentWorkloadDeploymentSpec(
            authProxyConfig({ QStringLiteral("development"), QStringLiteral("HTTPS://AUTH.EXAMPLE.COM/"),
                              QStringLiteral("https://ROUTER.EXAMPLE.COM/v1/"),
                              QStringLiteral("HTTPS://RUNTIME.EXAMPLE.COM/") }));
    const auto second = makeAgentWorkloadDeploymentSpec(
            authProxyConfig({ QStringLiteral("development"), QStringLiteral("https://auth.example.com"),
                              QStringLiteral("https://router.example.com/v1"),
                              QStringLiteral("https://runtime.example.com") }));

    QVERIFY(first);
    QVERIFY(second);
    QCOMPARE(first->backendProfile->authIssuer, QStringLiteral("https://auth.example.com"));
    QCOMPARE(first->backendProfile->routerBaseUrl, QStringLiteral("https://router.example.com/v1"));
    QCOMPARE(first->backendProfile->runtimeGatewayBaseUrl, QStringLiteral("https://runtime.example.com"));
    QCOMPARE(first->specHash(), second->specHash());
}

void AgentWorkloadDeploymentSpecTest::changesHashWhenDesiredStateChanges()
{
    const auto first = makeAgentWorkloadDeploymentSpec(authProxyConfig(completeCatalog().development));
    const auto second = makeAgentWorkloadDeploymentSpec(authProxyConfig(completeCatalog().production));

    QVERIFY(first);
    QVERIFY(second);
    QVERIFY(first->specHash() != second->specHash());
}

void AgentWorkloadDeploymentSpecTest::rendersOnlyWorkloadOwnedVariables()
{
    const auto proxy = makeAgentWorkloadDeploymentSpec(authProxyConfig(completeCatalog().development));
    const auto openClaw = makeAgentWorkloadDeploymentSpec(OpenClawCodexProtocolConfig {});

    QVERIFY(proxy);
    QVERIFY(openClaw);

    const auto proxyVariables = proxy->templateVariables();
    QCOMPARE(proxyVariables.value(QStringLiteral("$AMGPT_AUTH_ISSUER")), QStringLiteral("https://auth-dev.example.com"));
    QCOMPARE(proxyVariables.value(QStringLiteral("$AMGPT_ROUTER_BASE_URL")),
             QStringLiteral("https://router-dev.example.com/v1"));
    QVERIFY(!proxyVariables.contains(QStringLiteral("$AMGPT_DEVICE_GATEWAY_PORT")));

    const auto openClawVariables = openClaw->templateVariables();
    QVERIFY(!openClawVariables.contains(QStringLiteral("$OPENCLAW_CODEX_PORT")));
    QVERIFY(!openClawVariables.contains(QStringLiteral("$AMGPT_AUTH_ISSUER")));
    QVERIFY(!openClawVariables.contains(QStringLiteral("$AMGPT_ROUTER_BASE_URL")));
    QVERIFY(!openClawVariables.contains(QStringLiteral("$AMGPT_RUNTIME_GATEWAY_BASE_URL")));
    QVERIFY(!openClawVariables.contains(QStringLiteral("$AGENT_BACKEND_PROFILE")));
}

void AgentWorkloadDeploymentSpecTest::validatesCompleteCanonicalSpec()
{
    const auto proxy = makeAgentWorkloadDeploymentSpec(authProxyConfig(completeCatalog().development));
    const auto openClaw = makeAgentWorkloadDeploymentSpec(OpenClawCodexProtocolConfig {});
    QVERIFY(proxy);
    QVERIFY(openClaw);
    QCOMPARE(validateAgentWorkloadDeploymentSpec(*proxy), AgentDeploymentValidationError::None);
    QCOMPARE(validateAgentWorkloadDeploymentSpec(*openClaw), AgentDeploymentValidationError::None);

    auto changedImage = *proxy;
    changedImage.imageReference = QStringLiteral("docker.io/example/untrusted:v0.1.0");
    QCOMPARE(validateAgentWorkloadDeploymentSpec(changedImage), AgentDeploymentValidationError::InvalidDesiredState);

    auto changedEnvironment = *openClaw;
    changedEnvironment.environment.insert(QStringLiteral("UNDECLARED"), QStringLiteral("value"));
    QCOMPARE(validateAgentWorkloadDeploymentSpec(changedEnvironment),
             AgentDeploymentValidationError::InvalidDesiredState);

    auto publishedPort = *proxy;
    publishedPort.hostPort = QStringLiteral("8080");
    publishedPort.containerPort = QStringLiteral("8080");
    QCOMPARE(validateAgentWorkloadDeploymentSpec(publishedPort), AgentDeploymentValidationError::InvalidDesiredState);

    auto extraCapability = *openClaw;
    extraCapability.capabilitiesAdded.append(QStringLiteral("NET_ADMIN"));
    QCOMPARE(validateAgentWorkloadDeploymentSpec(extraCapability),
             AgentDeploymentValidationError::InvalidDesiredState);

    auto unsupported = *openClaw;
    unsupported.workload = QStringLiteral("unsupported");
    QCOMPARE(validateAgentWorkloadDeploymentSpec(unsupported), AgentDeploymentValidationError::UnsupportedWorkload);
}

QTEST_GUILESS_MAIN(AgentWorkloadDeploymentSpecTest)
#include "agent_workload_deployment_spec_test.moc"
