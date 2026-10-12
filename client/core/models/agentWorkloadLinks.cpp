#include "agentWorkloadLinks.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QUrl>

namespace amnezia
{
    namespace
    {
        bool exactKeys(const QJsonObject &object, const QSet<QString> &expected)
        {
            const auto keys = object.keys();
            return QSet<QString>(keys.cbegin(), keys.cend()) == expected;
        }

        // Consult only after JSON syntax validation; Qt otherwise silently keeps duplicate members.
        bool uniqueMembers(const QByteArray &data)
        {
            QList<QSet<QString>> objects;
            for (qsizetype i = 0; i < data.size(); ++i) {
                if (data[i] == '{') {
                    objects.append(QSet<QString> {});
                } else if (data[i] == '}') {
                    if (objects.isEmpty()) return false;
                    objects.removeLast();
                } else if (data[i] == '"') {
                    const auto start = i++;
                    while (i < data.size() && data[i] != '"') {
                        if (data[i] == '\\') ++i;
                        ++i;
                    }
                    auto next = i + 1;
                    while (next < data.size() && QByteArray(" \r\n\t").contains(data[next])) ++next;
                    if (next < data.size() && data[next] == ':') {
                        const auto key = QJsonDocument::fromJson("[" + data.mid(start, i - start + 1) + "]")
                                                 .array().first().toString();
                        if (objects.isEmpty() || objects.last().contains(key)) return false;
                        if (objects.size() == 1 && key == QStringLiteral("schema_version")) {
                            auto value = next + 1;
                            while (value < data.size() && QByteArray(" \r\n\t").contains(data[value])) ++value;
                            if (value + 1 >= data.size() || data[value] != '1'
                                || !QByteArray(" ,}\r\n\t").contains(data[value + 1])) return false;
                        }
                        objects.last().insert(key);
                    }
                }
            }
            return objects.isEmpty();
        }
    }

    bool isSafeAgentWorkloadWebUiHref(const QString &href)
    {
        if (href.isEmpty() || href.toUtf8().size() > 2048 || href.contains(QLatin1Char('\\'))) return false;
        for (const auto character : href) {
            if (character.isSpace() || character.unicode() < 0x20 || character.unicode() == 0x7f) return false;
        }
        const QUrl url(href, QUrl::StrictMode);
        if (!url.isValid() || url.isRelative() || url.host().isEmpty() || !url.userInfo().isEmpty()
            || url.authority(QUrl::FullyEncoded).contains(QLatin1Char('@'))) return false;
        if (url.scheme() == QStringLiteral("https")) return true;
        return url.scheme() == QStringLiteral("http")
                && (url.host() == QStringLiteral("localhost") || QHostAddress(url.host()).isLoopback());
    }

    AgentWorkloadLinksResult parseAgentWorkloadLinks(const QByteArray &payload, int exitCode)
    {
        AgentWorkloadLinksResult result;
        if (exitCode == 2) {
            result.error = AgentWorkloadLinksError::Unsupported;
            return result;
        }
        if (exitCode != 0) {
            result.error = AgentWorkloadLinksError::CommandFailed;
            return result;
        }
        result.error = AgentWorkloadLinksError::InvalidResponse;
        if (payload.size() > AgentWorkloadLinksMaxBytes) {
            result.error = AgentWorkloadLinksError::OutputTooLarge;
            return result;
        }
        QJsonParseError jsonError;
        const auto document = QJsonDocument::fromJson(payload, &jsonError);
        if (jsonError.error != QJsonParseError::NoError || !document.isObject()
            || payload.startsWith("\xef\xbb\xbf") || QString::fromUtf8(payload).toUtf8() != payload
            || !uniqueMembers(payload)) return result;
        const auto object = document.object();
        if (!exactKeys(object, { "schema_version", "links" })
            || object.value("schema_version") != QJsonValue(1)
            || !object.value("links").isArray()) return result;
        const auto links = object.value("links").toArray();
        if (links.size() > 1) return result;
        if (!links.isEmpty()) {
            if (!links.first().isObject()) return result;
            const auto link = links.first().toObject();
            if (!exactKeys(link, { "rel", "type", "href" }) || !link.value("rel").isString()
                || !link.value("type").isString()
                || !link.value("href").isString()) return result;
            const auto rel = link.value("rel").toString();
            const auto type = link.value("type").toString();
            const auto href = link.value("href").toString();
            if (rel.isEmpty() || rel.toUtf8().size() > 256 || type.isEmpty() || type.toUtf8().size() > 128
                || href.isEmpty() || href.toUtf8().size() > 2048) return result;
            // Unknown relations/media types do not acquire browser semantics.
            if (rel == QStringLiteral("urn:amnezia-gpt:link-relation:client-launch:v1")
                && type == QStringLiteral("text/html")) {
                if (!isSafeAgentWorkloadWebUiHref(href)) return result;
                result.webUi = href;
            }
        }
        result.error = AgentWorkloadLinksError::None;
        return result;
    }
}
