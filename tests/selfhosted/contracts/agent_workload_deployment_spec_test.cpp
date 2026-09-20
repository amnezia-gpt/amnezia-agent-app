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
              QStringLiteral("https://router.example.com/v1") },
            { QStringLiteral("development"), QStringLiteral("https://auth-dev.example.com"),
              QStringLiteral("https://router-dev.example.com/v1") },
        };
    }

    AmgptAuthProxyProtocolConfig authProxyConfig(const AgentBackendProfile &profile, const QString &port = {})
    {
        AmgptAuthProxyProtocolConfig config;
        config.port = port;
        config.backendProfile = profile.id;
        config.authIssuer = profile.authIssuer;
        config.routerBaseUrl = profile.routerBaseUrl;
        return config;
    }
} // namespace

class AgentWorkloadDeploymentSpecTest : public QObject
{
    Q_OBJECT

private slots:
    void resolvesBackendProfilesAtomically();
    void rejectsIncompleteBackendProfiles();
    void rejectsUnknownBackendEnvironment();
    void persistsResolvedBackendProfile();
    void createsCompleteAuthProxyDesiredState();
    void keepsOpenClawIndependentFromBackendProfile();
    void rejectsMalformedProfileUrls_data();
    void rejectsMalformedProfileUrls();
    void rejectsInvalidPorts_data();
    void rejectsInvalidPorts();
    void normalizesEquivalentProfilesBeforeHashing();
    void changesHashWhenDesiredStateChanges();
    void rendersOnlyWorkloadOwnedVariables();
    void validatesCompleteCanonicalSpec();
};

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
}

void AgentWorkloadDeploymentSpecTest::createsCompleteAuthProxyDesiredState()
{
    AgentDeploymentValidationError error = AgentDeploymentValidationError::None;
    const auto spec = makeAgentWorkloadDeploymentSpec(authProxyConfig(completeCatalog().production), &error);

    QVERIFY(spec);
    QCOMPARE(error, AgentDeploymentValidationError::None);
    QCOMPARE(spec->schemaVersion, 1);
    QCOMPARE(spec->workloadVersion, QStringLiteral("v0.2.1"));
    QCOMPARE(spec->imageReference, QStringLiteral("docker.io/amneziavpn/agent-workload-amgpt-auth-proxy:v0.2.1"));
    QCOMPARE(spec->platform, QStringLiteral("linux/amd64"));
    QCOMPARE(spec->containerName, QStringLiteral("amnezia-amgpt-auth-proxy"));
    QCOMPARE(spec->networkName, QStringLiteral("amnezia-agent-workloads"));
    QCOMPARE(spec->hostPort, QStringLiteral("8080"));
    QCOMPARE(spec->containerPort, QStringLiteral("8080"));
    QCOMPARE(spec->restartPolicy, QStringLiteral("unless-stopped"));
    QCOMPARE(spec->environment.value(QStringLiteral("AMGPT_AUTH_ISSUER")), QStringLiteral("https://auth.example.com"));
    QCOMPARE(spec->environment.value(QStringLiteral("AMGPT_ROUTER_BASE_URL")),
             QStringLiteral("https://router.example.com/v1"));
    QCOMPARE(spec->volumes.size(), 1);
    QCOMPARE(spec->volumes.constFirst().target, QStringLiteral("/var/lib/amgpt-auth-proxy"));
    QCOMPARE(spec->capabilitiesDropped, QStringList { QStringLiteral("ALL") });
    QCOMPARE(spec->securityOptions, QStringList { QStringLiteral("no-new-privileges") });
    QCOMPARE(spec->healthCheck.command,
             QStringList({ QStringLiteral("/usr/local/bin/amgpt-auth-proxy"), QStringLiteral("healthcheck") }));

    const auto labels = spec->deploymentLabels();
    QCOMPARE(labels.size(), 6);
    QCOMPARE(labels.value(QStringLiteral("org.amnezia.amgpt.deployment.managed-by")),
             QStringLiteral("amnezia-agent-app"));
    QCOMPARE(labels.value(QStringLiteral("org.amnezia.amgpt.deployment.workload")), QStringLiteral("amgpt-auth-proxy"));
    QCOMPARE(labels.value(QStringLiteral("org.amnezia.amgpt.deployment.schema-version")), QStringLiteral("1"));
    QCOMPARE(labels.value(QStringLiteral("org.amnezia.amgpt.deployment.workload-version")), QStringLiteral("v0.2.1"));
    QCOMPARE(labels.value(QStringLiteral("org.amnezia.amgpt.deployment.backend-profile")), QStringLiteral("production"));
    QCOMPARE(labels.value(QStringLiteral("org.amnezia.amgpt.deployment.spec-hash")), spec->specHash());
    QCOMPARE(spec->specHash().size(), 64);
}

void AgentWorkloadDeploymentSpecTest::keepsOpenClawIndependentFromBackendProfile()
{
    OpenClawCodexProtocolConfig config;
    config.port = QStringLiteral("28789");

    const auto spec = makeAgentWorkloadDeploymentSpec(config);

    QVERIFY(spec);
    QCOMPARE(spec->hostPort, QStringLiteral("28789"));
    QCOMPARE(spec->containerPort, QStringLiteral("18789"));
    QVERIFY(!spec->backendProfile);
    QVERIFY(!spec->environment.contains(QStringLiteral("AMGPT_AUTH_ISSUER")));
    QVERIFY(!spec->environment.contains(QStringLiteral("AMGPT_ROUTER_BASE_URL")));
    QVERIFY(!spec->deploymentLabels().contains(QStringLiteral("org.amnezia.amgpt.deployment.backend-profile")));
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
    const auto spec = makeAgentWorkloadDeploymentSpec(authProxyConfig({ profileId, authIssuer, routerBaseUrl }), &error);

    QVERIFY(!spec);
    QCOMPARE(error, expectedError);
}

void AgentWorkloadDeploymentSpecTest::rejectsInvalidPorts_data()
{
    QTest::addColumn<QString>("port");

    QTest::newRow("zero") << QStringLiteral("0");
    QTest::newRow("too-large") << QStringLiteral("65536");
    QTest::newRow("negative") << QStringLiteral("-1");
    QTest::newRow("leading-zero") << QStringLiteral("08080");
    QTest::newRow("not-a-number") << QStringLiteral("https");
    QTest::newRow("surrounding-space") << QStringLiteral(" 8080");
}

void AgentWorkloadDeploymentSpecTest::rejectsInvalidPorts()
{
    QFETCH(QString, port);

    AgentDeploymentValidationError error = AgentDeploymentValidationError::None;
    const auto spec = makeAgentWorkloadDeploymentSpec(authProxyConfig(completeCatalog().development, port), &error);

    QVERIFY(!spec);
    QCOMPARE(error, AgentDeploymentValidationError::InvalidPort);
}

void AgentWorkloadDeploymentSpecTest::normalizesEquivalentProfilesBeforeHashing()
{
    const auto first = makeAgentWorkloadDeploymentSpec(
            authProxyConfig({ QStringLiteral("development"), QStringLiteral("HTTPS://AUTH.EXAMPLE.COM/"),
                              QStringLiteral("https://ROUTER.EXAMPLE.COM/v1/") }));
    const auto second = makeAgentWorkloadDeploymentSpec(
            authProxyConfig({ QStringLiteral("development"), QStringLiteral("https://auth.example.com"),
                              QStringLiteral("https://router.example.com/v1") }));

    QVERIFY(first);
    QVERIFY(second);
    QCOMPARE(first->backendProfile->authIssuer, QStringLiteral("https://auth.example.com"));
    QCOMPARE(first->backendProfile->routerBaseUrl, QStringLiteral("https://router.example.com/v1"));
    QCOMPARE(first->specHash(), second->specHash());
}

void AgentWorkloadDeploymentSpecTest::changesHashWhenDesiredStateChanges()
{
    const auto first =
            makeAgentWorkloadDeploymentSpec(authProxyConfig(completeCatalog().development, QStringLiteral("18080")));
    const auto second =
            makeAgentWorkloadDeploymentSpec(authProxyConfig(completeCatalog().development, QStringLiteral("18081")));

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
    QCOMPARE(proxyVariables.value(QStringLiteral("$AMGPT_AUTH_PROXY_PORT")), QStringLiteral("8080"));

    const auto openClawVariables = openClaw->templateVariables();
    QCOMPARE(openClawVariables.value(QStringLiteral("$OPENCLAW_CODEX_PORT")), QStringLiteral("18789"));
    QVERIFY(!openClawVariables.contains(QStringLiteral("$AMGPT_AUTH_ISSUER")));
    QVERIFY(!openClawVariables.contains(QStringLiteral("$AMGPT_ROUTER_BASE_URL")));
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

    auto unsupported = *openClaw;
    unsupported.workload = QStringLiteral("unsupported");
    QCOMPARE(validateAgentWorkloadDeploymentSpec(unsupported), AgentDeploymentValidationError::UnsupportedWorkload);
}

QTEST_GUILESS_MAIN(AgentWorkloadDeploymentSpecTest)
#include "agent_workload_deployment_spec_test.moc"
