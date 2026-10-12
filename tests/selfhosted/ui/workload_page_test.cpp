#include <QtTest>
#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>

#include <memory>

#include "ui/utils/pageEnum.h"

class WorkloadPageServices final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int processedContainerIndex READ containerIndex CONSTANT)
    Q_PROPERTY(QString processedServerId READ serverId CONSTANT)
    Q_PROPERTY(int safeAreaTopMargin READ zero CONSTANT)
    Q_PROPERTY(QString agentWorkloadEnvironment READ environment CONSTANT)
    Q_PROPERTY(QVariantMap localAgentBackendProfile READ backend CONSTANT)
    Q_PROPERTY(QString gatewayEndpoint READ gateway CONSTANT)
    Q_PROPERTY(bool isDevGatewayEnv READ devGateway CONSTANT)

public:
    QString environment() const { return QStringLiteral("local"); }
    QString gateway() const { return QStringLiteral("https://gateway.example"); }
    bool devGateway() const { return false; }
    QVariantMap backend() const { return {}; }
    int profileSaves = 0;
    int linksRequests = 0;
    int webUiRequests = 0;
    int statusRequests = 0;
    int loginRequests = 0;
    int browserOpens = 0;
    int closedPages = 0;
    int containerIndex() const { return 16; }
    QString serverId() const { return QStringLiteral("test-server"); }
    int zero() const { return 0; }
    Q_INVOKABLE QVariantMap getContainerConfig(int) const
    {
        return { { "container", "amnezia-amgpt-device-gateway" },
                 { "amgptdevicegateway", QVariantMap { { "backend_profile", "local" } } } };
    }
    Q_INVOKABLE QString getProcessedContainerName() const { return QStringLiteral("Device Gateway"); }
    Q_INVOKABLE int getLineHeightAppend() const { return 0; }
    Q_INVOKABLE void resetRootObject() {}
    Q_INVOKABLE void setFocusOnDefaultItem() {}
    Q_INVOKABLE void showNotificationMessage(const QString &) {}
    Q_INVOKABLE void closePage() { ++closedPages; }
    Q_INVOKABLE void refreshAgentWorkloadState(const QString &, int)
    {
        emit agentWorkloadStateRefreshed(1, 0, QStringLiteral("Running"));
    }
    Q_INVOKABLE void startAgentWorkloadLogin(const QString &, int)
    {
        ++loginRequests;
        emit serverIsBusy(true);
    }
    Q_INVOKABLE void refreshAgentWorkloadLoginStatus(const QString &, int)
    {
        ++statusRequests;
        emit serverIsBusy(true);
    }
    Q_INVOKABLE void refreshAgentWorkloadLinks(const QString &serverId)
    {
        ++linksRequests;
        emit agentWorkloadLinksRefreshed(serverId, false);
    }
    Q_INVOKABLE void openAgentWorkloadWebInterface(const QString &)
    {
        ++webUiRequests;
        emit serverIsBusy(true);
    }
    Q_INVOKABLE bool saveLocalAgentBackendProfile(const QString &, const QString &, const QString &,
                                                 const QString &, const QString &)
    {
        ++profileSaves;
        return true;
    }
    Q_INVOKABLE QString publicTrustBundleFromClipboard() const { return {}; }
    Q_INVOKABLE bool openAgentWorkloadVerificationUrl(const QString &)
    {
        ++browserOpens;
        return true;
    }
    void presentLogin()
    {
        emit serverIsBusy(false);
        emit agentWorkloadLoginStarted(1, 0, 0, QStringLiteral("https://auth.example/device"),
                                      QStringLiteral("TEST-CODE"), 600, QStringLiteral("Approve in browser"));
    }

signals:
    void agentWorkloadLinksRefreshed(const QString &serverId, bool available);
    void agentWorkloadWebInterfaceOpened(const QString &serverId, bool opened, const QString &message);
    void serverIsBusy(bool busy);
    void agentWorkloadStateRefreshed(int action, int reason, const QString &message);
    void agentWorkloadReconcileFinished(int action, int reason, int status, int applyReason, int transportError, const QString &message);
    void agentWorkloadLifecycleFinished(int status, int reason, int transportError, const QString &message);
    void agentWorkloadLoginStarted(int mode, int precondition, int error, const QString &url, const QString &code, int expires, const QString &message);
    void agentWorkloadLoginStatusRefreshed(int mode, int state, bool authenticated, int error, const QString &message);
};

class WorkloadPageTest final : public QObject
{
    Q_OBJECT
    WorkloadPageServices services;
    std::unique_ptr<QQmlEngine> engine;
    std::unique_ptr<QQuickWindow> window;
    std::unique_ptr<QQuickItem> page;

    QQuickItem *control(const QString &text)
    {
        QList<QQuickItem *> items { page.get() };
        while (!items.isEmpty()) {
            auto *item = items.takeLast();
            items.append(item->childItems());
            if (item->property("text").toString() == text && (item->property("clickedFunction").isValid() || item->property("clickedFunc").isValid()))
                return item;
        }
        return nullptr;
    }

    void capturePage(const QString &name)
    {
        const auto directory = qEnvironmentVariable("AMNEZIA_UI_EVIDENCE_DIR");
        if (!directory.isEmpty())
            QVERIFY(window->grabWindow().save(QDir(directory).filePath(name)));
    }

    void click(QQuickItem *item)
    {
        QVERIFY(item);
        QTRY_VERIFY(item->width() > 0 && item->height() > 0);
        QTest::mouseClick(window.get(), Qt::LeftButton, Qt::NoModifier,
                         item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
    }

private slots:
    void initTestCase()
    {
        qmlRegisterUncreatableMetaObject(PageLoader::staticMetaObject, "PageEnum", 1, 0, "PageEnum", "Enums only");
    }
    void init()
    {
        services.profileSaves = services.linksRequests = 0;
        services.webUiRequests = services.statusRequests = services.loginRequests = services.browserOpens = services.closedPages = 0;
        engine = std::make_unique<QQmlEngine>();
        engine->addImportPath(QStringLiteral(AMNEZIA_SOURCE_ROOT "/client/ui/qml/Modules"));
        for (const auto *name : { "InstallController", "ContainersModel", "ServersUiController", "PageController",
                                  "LanguageUiController", "FocusController", "SettingsController" })
            engine->rootContext()->setContextProperty(QString::fromLatin1(name), &services);
        QQmlComponent component(engine.get(), QUrl::fromLocalFile(QStringLiteral(
            AMNEZIA_SOURCE_ROOT "/client/ui/qml/Pages2/PageServiceAgentWorkloadSettings.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        page.reset(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY2(page, qPrintable(component.errorString()));
        page->setProperty("enableTimer", false);
        window = std::make_unique<QQuickWindow>();
        window->setColor(Qt::black);
        window->resize(700, 1500);
        page->setParentItem(window->contentItem());
        page->setSize(QSizeF(700, 1500));
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        QTRY_VERIFY(control(QStringLiteral("Sign in with Amnezia GPT")));
        click(control(QStringLiteral("Sign in with Amnezia GPT")));
        QCOMPARE(services.loginRequests, 1);
        services.presentLogin();
        QTRY_VERIFY(control(QStringLiteral("Open authorization page"))->isVisible());
    }
    void cleanup()
    {
        page.reset();
        window.reset();
        engine.reset();
    }
    void webInterfaceIsAnnouncedAndRecheckedOnClick()
    {
        QCOMPARE(services.linksRequests, 1); // One page-entry read, no browser opening.
        QCOMPARE(services.webUiRequests, 0);
        auto *button = control(QStringLiteral("Open WebUI"));
        QVERIFY(button);
        QVERIFY(!button->isVisible());
        emit services.agentWorkloadLinksRefreshed("different-server", true);
        QVERIFY(!button->isVisible());
        emit services.agentWorkloadLinksRefreshed("test-server", true);
        QVERIFY(button->isVisible());
        capturePage(QStringLiteral("workload-links-available.png"));
        click(button);
        QCOMPARE(services.webUiRequests, 1);
        QVERIFY(!button->isEnabled());
        click(control(QStringLiteral("Open authorization page")));
        QCOMPARE(services.browserOpens, 1);
        QVERIFY(!control(QStringLiteral("Remove workload"))->isEnabled());
        // A fresh read at click can discover logout/absence: discard the old availability.
        emit services.serverIsBusy(false);
        emit services.agentWorkloadLinksRefreshed("test-server", false);
        emit services.agentWorkloadWebInterfaceOpened("test-server", false, "Unavailable");
        QVERIFY(!button->isVisible());
        QVERIFY(page->property("webInterfaceUrl").isValid() == false);
        QVERIFY(!page->property("linksMessage").isValid());
        click(control(QStringLiteral("Refresh state")));
        QCOMPARE(services.linksRequests, 2);
        QCOMPARE(services.webUiRequests, 1);
        capturePage(QStringLiteral("workload-links-absent.png"));
    }
    void localDevMenuDoesNotConfigureWebInterface()
    {
        QQmlComponent component(engine.get(), QUrl::fromLocalFile(QStringLiteral(
            AMNEZIA_SOURCE_ROOT "/client/ui/qml/Pages2/PageDevMenu.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        page.reset(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY2(page, qPrintable(component.errorString()));
        page->setProperty("enableTimer", false);
        window->resize(700, 2600);
        page->setParentItem(window->contentItem());
        page->setSize(QSizeF(700, 2600));
        QVERIFY(!page->findChild<QQuickItem *>(QStringLiteral("localWebUiField")));
        click(control(QStringLiteral("Save local backend")));
        QCOMPARE(services.profileSaves, 1); // Exactly the existing five workload profile fields.
        capturePage(QStringLiteral("dev-menu-workload-profile.png"));
    }
    void pendingLoginDoesNotScheduleStatusRequests()
    {
        // Deliver scheduled repeating-timer ticks deterministically, without a wall-clock sleep.
        for (auto *object : page->findChildren<QObject *>()) {
            if (object->property("repeat").toBool() && object->property("running").toBool())
                QVERIFY(QMetaObject::invokeMethod(object, "triggered"));
        }
        QCOMPARE(services.statusRequests, 0);
    }
    void browserRemainsClickableDuringStatusRequest()
    {
        click(control(QStringLiteral("Refresh Amnezia GPT sign-in status")));
        QCOMPARE(services.statusRequests, 1);
        click(control(QStringLiteral("Open authorization page")));
        QCOMPARE(services.browserOpens, 1);
        click(control(QStringLiteral("Refresh Amnezia GPT sign-in status")));
        QCOMPARE(services.statusRequests, 1); // No overlapping SSH request.
    }
    void navigationRemainsClickableDuringStatusRequest()
    {
        click(control(QStringLiteral("Refresh Amnezia GPT sign-in status")));
        QCOMPARE(services.statusRequests, 1);
        QQuickItem *back = nullptr;
        for (auto *item : page->findChildren<QQuickItem *>()) {
            if (item->property("image").toString() == QStringLiteral("qrc:/images/controls/arrow-left.svg")) {
                back = item;
                break;
            }
        }
        click(back);
        QCOMPARE(services.closedPages, 1);
    }
    void failedStatusRequestPreservesPendingPresentation()
    {
        click(control(QStringLiteral("Refresh Amnezia GPT sign-in status")));
        emit services.serverIsBusy(false);
        emit services.agentWorkloadLoginStatusRefreshed(1, 3, false, 1, QStringLiteral("Status unavailable"));
        QCOMPARE(page->property("verificationUrl").toString(), QStringLiteral("https://auth.example/device"));
        QCOMPARE(page->property("userCode").toString(), QStringLiteral("TEST-CODE"));
        click(control(QStringLiteral("Open authorization page")));
        QCOMPARE(services.browserOpens, 1);
    }
    void confirmedReadyClearsPendingPresentation()
    {
        emit services.agentWorkloadLoginStatusRefreshed(1, 2, true, 0, QStringLiteral("Ready"));
        QVERIFY(page->property("verificationUrl").toString().isEmpty());
        QVERIFY(page->property("userCode").toString().isEmpty());
        QCOMPARE(services.linksRequests, 1); // Sign-in status does not trigger a links read.
    }
};

QTEST_MAIN(WorkloadPageTest)
#include "workload_page_test.moc"
