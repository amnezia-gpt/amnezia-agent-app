#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include "core/models/agentWorkloadLinks.h"

using namespace amnezia;

namespace
{
    QByteArray linksDocument(const QJsonArray &links = {})
    {
        return QJsonDocument(QJsonObject { { "schema_version", 1 }, { "links", links } })
                .toJson(QJsonDocument::Compact);
    }
}

class AgentWorkloadLinksTest : public QObject
{
    Q_OBJECT
private slots:
    void safeUrls_data();
    void safeUrls();
    void rejectsMalformedDocuments_data();
    void rejectsMalformedDocuments();
    void ignoresUnknownRelationOrMediaTypeAndBoundsResponse();
    void acceptsAnnouncedUrlWithoutKnowingBackend();
    void acceptsNormalAbsenceWithoutLink();
    void acceptsOpaqueLaunchQueries_data();
    void acceptsOpaqueLaunchQueries();
    void nonzeroExitCannotAnnounceLink();
    void errorDocumentCannotBecomeEmptySuccess();
};

void AgentWorkloadLinksTest::safeUrls_data()
{
    QTest::addColumn<QString>("href");
    QTest::addColumn<bool>("safe");
    QTest::newRow("https-arbitrary-route") << QString("https://ui.example/a/b#/thread/123") << true;
    QTest::newRow("localhost") << QString("http://localhost:4173/custom/#/") << true;
    QTest::newRow("ipv4-loopback-range") << QString("http://127.19.2.3:3000/#/") << true;
    QTest::newRow("ipv6-loopback") << QString("http://[::1]:5173/#/") << true;
    QTest::newRow("unicode") << QString::fromUtf8("https://ui.example/устройство#страница") << true;
    QTest::newRow("empty-query") << QString("https://ui.example/?") << true;
    QTest::newRow("at-in-path") << QString("https://ui.example/path@name") << true;
    QTest::newRow("at-in-query") << QString("https://ui.example/?hint=user@example.test") << true;
    QTest::newRow("empty-userinfo") << QString("https://@ui.example/") << false;
    QTest::newRow("encoded-userinfo") << QString("https://user%40example.test@ui.example/") << false;
    QTest::newRow("malformed-query-escape") << QString("https://ui.example/?x=%ZZ") << false;
    const QString prefix = "https://ui.example/";
    QTest::newRow("max-bytes") << prefix + QString(2048 - prefix.size(), 'a') << true;
    QTest::newRow("too-long") << prefix + QString(2049 - prefix.size(), 'a') << false;
    QTest::newRow("unicode-too-many-bytes") << prefix + QString(1024, QChar(0x0430)) << false;
    for (const auto &href : { QString("http://ui.example/#/"), QString("http://localhost.example/#/"),
            QString("http://128.0.0.1/#/"), QString("http://[::2]/#/"), QString("https://user:pass@ui.example/"),
            QString("https://ui.example/path\\other"),
            QString("javascript:alert(1)"), QString("file:///tmp/a"), QString("/relative"), QString(""),
            QString("https://ui.example/\n"), QString("https://ui.example/with space"),
            QString("https://ui.example:999999/") })
        QTest::newRow(qPrintable(href)) << href << false;
    QTest::newRow("unicode-space") << QString("https://ui.example/") + QChar(0x00a0) << false;
    QTest::newRow("control") << QString("https://ui.example/") + QChar(0x7f) << false;
}

void AgentWorkloadLinksTest::safeUrls()
{
    QFETCH(QString, href); QFETCH(bool, safe);
    QCOMPARE(isSafeAgentWorkloadWebUiHref(href), safe);
}

void AgentWorkloadLinksTest::rejectsMalformedDocuments_data()
{
    QTest::addColumn<QByteArray>("payload");
    auto object = QJsonDocument::fromJson(linksDocument({
        QJsonObject { { "rel", "urn:amnezia-gpt:link-relation:client-launch:v1" }, { "type", "text/html" }, { "href", "https://ui.example/#/" } }
    })).object();
    const auto row = [&](const char *name, const QJsonObject &value) {
        QTest::newRow(name) << QJsonDocument(value).toJson();
    };
    auto bad = object; bad["schema_version"] = 2; row("schema-version", bad);
    bad = object; bad["schema_version"] = "1"; row("schema-string", bad);
    bad = object; bad["extra"] = true; row("extra-field", bad);
    bad = object; bad["links"] = QJsonValue::Null; row("null-links", bad);
    bad = object; bad.remove("links"); row("missing-links", bad);
    bad = object; bad["state"] = "available"; row("legacy-state", bad);
    bad = object; bad["error_code"] = "gateway-unavailable"; row("links-with-error", bad);
    bad = object; bad["links"] = QJsonArray { object["links"].toArray().first(), object["links"].toArray().first() }; row("two-links", bad);
    bad = object; bad["links"] = QJsonArray { 42 }; row("non-object-link", bad);
    bad = object; bad["links"] = QJsonArray { QJsonObject { { "rel", "urn:amnezia-gpt:link-relation:client-launch:v1" }, { "type", "text/html" }, { "href", "http://evil.example" } } }; row("unsafe-href", bad);
    bad = object; bad["links"] = QJsonArray { QJsonObject { { "rel", "urn:amnezia-gpt:link-relation:client-launch:v1" }, { "type", "text/html" }, { "href", 42 } } }; row("wrong-href-type", bad);
    bad = object; bad["links"] = QJsonArray { QJsonObject { { "rel", "urn:amnezia-gpt:link-relation:client-launch:v1" }, { "type", "text/html" }, { "href", "https://ui.example" }, { "extra", true } } }; row("extra-link-field", bad);
    bad = object; bad["links"] = QJsonArray { QJsonObject { { "kind", "web-ui" }, { "href", "https://ui.example/" } } }; row("legacy-kind", bad);
    for (const auto *field : { "rel", "type", "href" }) {
        auto link = object["links"].toArray().first().toObject();
        link.remove(field); bad = object; bad["links"] = QJsonArray { link }; row(qPrintable(QString("missing-") + field), bad);
        link = object["links"].toArray().first().toObject();
        link[field] = 42; bad["links"] = QJsonArray { link }; row(qPrintable(QString("nonstring-") + field), bad);
    }
    for (const auto *field : { "rel", "type", "href" }) {
        auto link = object["links"].toArray().first().toObject();
        link[field] = ""; bad = object; bad["links"] = QJsonArray { link }; row(qPrintable(QString("empty-") + field), bad);
        const int maxBytes = QString(field) == "rel" ? 256 : QString(field) == "type" ? 128 : 2048;
        link[field] = QString(maxBytes + 1, 'a'); bad["links"] = QJsonArray { link }; row(qPrintable(QString("oversized-") + field), bad);
    }
    QTest::newRow("duplicate-link-rel") << QByteArray(R"({"schema_version":1,"links":[{"rel":"urn:amnezia-gpt:link-relation:client-launch:v1","rel":"urn:example:future","type":"text/html","href":"https://ui.example"}]})");
    QTest::newRow("duplicate-link-type") << QByteArray(R"({"schema_version":1,"links":[{"rel":"urn:amnezia-gpt:link-relation:client-launch:v1","type":"text/html","type":"application/json","href":"https://ui.example"}]})");
    QTest::newRow("trailing-output") << QJsonDocument(object).toJson() + "junk";
    QTest::newRow("two-documents") << QJsonDocument(object).toJson() + "{}";
    QTest::newRow("truncated") << QByteArray("{");
    QTest::newRow("array-root") << QByteArray("[]");
    QTest::newRow("duplicate") << QByteArray(R"({"schema_version":1,"links":[],"links":[]})");
    QTest::newRow("escaped-duplicate") << QByteArray(R"({"schema_version":1,"links":[],"lin\u006bs":[]})");
    QTest::newRow("duplicate-link-href") << QByteArray(R"({"schema_version":1,"links":[{"rel":"urn:amnezia-gpt:link-relation:client-launch:v1","type":"text/html","href":"https://ui.example","href":"https://other.example"}]})");
    QTest::newRow("float-schema") << QByteArray(R"({"schema_version":1.0,"links":[]})");
    QTest::newRow("bom") << QByteArray("\xef\xbb\xbf") + linksDocument();
}

void AgentWorkloadLinksTest::rejectsMalformedDocuments()
{
    QFETCH(QByteArray, payload);
    const auto result = parseAgentWorkloadLinks(payload, 0);
    QCOMPARE(result.error, AgentWorkloadLinksError::InvalidResponse);
    QVERIFY(!result.webUi);
}

void AgentWorkloadLinksTest::ignoresUnknownRelationOrMediaTypeAndBoundsResponse()
{
    for (const auto &link : {
        QJsonObject { { "rel", "urn:example:future-interface" }, { "type", "text/html" }, { "href", "ssh://ignored.example" } },
        QJsonObject { { "rel", "urn:amnezia-gpt:link-relation:client-launch:v1" }, { "type", "application/json" }, { "href", "https://ui.example/" } }
    }) {
        const auto result = parseAgentWorkloadLinks(linksDocument({ link }), 0);
        QCOMPARE(result.error, AgentWorkloadLinksError::None);
        QVERIFY(!result.webUi);
    }
    QCOMPARE(parseAgentWorkloadLinks(QByteArray(8193, 'a'), 0).error, AgentWorkloadLinksError::OutputTooLarge);
}

void AgentWorkloadLinksTest::acceptsAnnouncedUrlWithoutKnowingBackend()
{
    const QString href = "https://console.example/custom-route/#/arbitrary-view";
    const auto result = parseAgentWorkloadLinks(linksDocument({
        QJsonObject { { "rel", "urn:amnezia-gpt:link-relation:client-launch:v1" }, { "type", "text/html" }, { "href", href } }
    }), 0);
    QCOMPARE(result.error, AgentWorkloadLinksError::None);
    QVERIFY(result.webUi);
    QCOMPARE(*result.webUi, href);
}

void AgentWorkloadLinksTest::acceptsOpaqueLaunchQueries_data()
{
    QTest::addColumn<QString>("href");
    const QString query = "?iss=https%3A%2F%2Fauth.example&target_resource=%2F%2Fdevices.amnezia-gpt%2Fdevices%2Fdev_test";
    QTest::newRow("dev-launch") << QString("https://console.example/login") + query;
    QTest::newRow("local-launch") << QString("http://localhost:5173/login") + query;
    QTest::newRow("ipv4-launch") << QString("http://127.19.2.3:5173/login") + query;
    QTest::newRow("ipv6-launch") << QString("http://[::1]:5173/login") + query;
    QTest::newRow("opaque-parameters") << QString("https://console.example/custom?future=a%26b%3Dc+z&future=second#/view");
}

void AgentWorkloadLinksTest::acceptsOpaqueLaunchQueries()
{
    QFETCH(QString, href);
    const auto result = parseAgentWorkloadLinks(linksDocument({
        QJsonObject { { "rel", "urn:amnezia-gpt:link-relation:client-launch:v1" }, { "type", "text/html" }, { "href", href } }
    }), 0);
    QCOMPARE(result.error, AgentWorkloadLinksError::None);
    QVERIFY(result.webUi);
    QCOMPARE(*result.webUi, href);
    // The same QUrl conversion used at the system-browser boundary preserves encoded parameters.
    QCOMPARE(QUrl(*result.webUi, QUrl::StrictMode).toEncoded(), href.toUtf8());
}

void AgentWorkloadLinksTest::acceptsNormalAbsenceWithoutLink()
{
    const auto result = parseAgentWorkloadLinks(linksDocument(), 0);
    QCOMPARE(result.error, AgentWorkloadLinksError::None);
    QVERIFY(!result.webUi);
}

void AgentWorkloadLinksTest::nonzeroExitCannotAnnounceLink()
{
    const auto payload = linksDocument({
        QJsonObject { { "rel", "urn:amnezia-gpt:link-relation:client-launch:v1" }, { "type", "text/html" }, { "href", "https://console.example/" } }
    });
    QVERIFY(!parseAgentWorkloadLinks(payload, 1).webUi);
    QCOMPARE(parseAgentWorkloadLinks(payload, 2).error, AgentWorkloadLinksError::Unsupported);
}

void AgentWorkloadLinksTest::errorDocumentCannotBecomeEmptySuccess()
{
    const QByteArray error = R"({"schema_version":1,"error_code":"gateway-unavailable"})";
    const auto failed = parseAgentWorkloadLinks(error, 1);
    QCOMPARE(failed.error, AgentWorkloadLinksError::CommandFailed);
    QVERIFY(!failed.webUi);
    // Even a missing/wrong exit status must not turn the error document into normal absence.
    const auto wrongExit = parseAgentWorkloadLinks(error, 0);
    QCOMPARE(wrongExit.error, AgentWorkloadLinksError::InvalidResponse);
    QVERIFY(!wrongExit.webUi);
    QCOMPARE(parseAgentWorkloadLinks(linksDocument(), 1).error, AgentWorkloadLinksError::CommandFailed);
    QCOMPARE(parseAgentWorkloadLinks(R"({"schema_version":1,"error_code":"invalid-request"})", 2).error,
             AgentWorkloadLinksError::Unsupported);
}

QTEST_GUILESS_MAIN(AgentWorkloadLinksTest)
#include "agent_workload_links_test.moc"
