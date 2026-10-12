#pragma once

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

// Public P-256 generator point: test data only, never a deployment signer.
inline QString publicTrustFixture(const QString &issuer, const QString &kid = QStringLiteral("test-signer"))
{
    const auto x = QByteArray::fromHex("6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296");
    const auto y = QByteArray::fromHex("4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5");
    return QString::fromUtf8(QJsonDocument(QJsonObject {
        { "schema_version", 1 }, { "issuer", issuer },
        { "keys", QJsonArray { QJsonObject {
            { "kty", "EC" }, { "crv", "P-256" }, { "use", "sig" }, { "alg", "ES256" },
            { "kid", kid }, { "x", QString::fromLatin1(x.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)) },
            { "y", QString::fromLatin1(y.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)) }
        } } }
    }).toJson(QJsonDocument::Indented));
}

inline QString trustPin(const QString &bundle)
{
    return QString::fromLatin1(QCryptographicHash::hash(bundle.toUtf8(), QCryptographicHash::Sha256).toHex());
}

template <typename Profile> Profile withPublicTrust(Profile profile, const QString &kid = QStringLiteral("test-signer"))
{
    auto url = QUrl(profile.authIssuer);
    url.setScheme(url.scheme().toLower());
    auto issuer = url.toString();
    if (issuer.endsWith('/')) issuer.chop(1);
    profile.ingressTrustBundle = publicTrustFixture(issuer, kid);
    profile.ingressTrustSha256 = trustPin(profile.ingressTrustBundle);
    return profile;
}

inline QJsonArray observedTrustEnvironment(const amnezia::AgentWorkloadDeploymentSpec &spec)
{
    QJsonArray result;
    for (const QString &name : { QString("AMGPT_AUTH_ISSUER"), QString("AMGPT_ROUTER_BASE_URL"),
                                QString("AMGPT_RUNTIME_GATEWAY_BASE_URL"), QString("AMGPT_INGRESS_TRUST_BUNDLE"),
                                QString("AMGPT_INGRESS_TRUST_SHA256") }) {
        if (spec.environment.contains(name)) result.append(name + "=" + spec.environment.value(name));
    }
    return result;
}
